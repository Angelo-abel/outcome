/* unittests.cpp
Unit testing for memory transactions
(C) 2013-2014 Niall Douglas http://www.nedproductions.biz/


Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#include "spinlock.hpp"
#include "timing.h"

#include <stdio.h>
#include <unordered_map>
#include <algorithm>

#ifdef _MSC_VER
//#define BOOST_HAVE_SYSTEM_CONCURRENT_UNORDERED_MAP
#endif

#ifdef BOOST_HAVE_SYSTEM_CONCURRENT_UNORDERED_MAP
#include <concurrent_unordered_map.h>
#endif

#ifndef BOOST_MEMORY_TRANSACTIONS_DISABLE_CATCH
#define CATCH_CONFIG_RUNNER
#include "catch.hpp"
#endif



using namespace std;

TEST_CASE("works/spinlock", "Tests that the spinlock works as intended")
{
  boost::spinlock::spinlock<bool> lock;
  REQUIRE(lock.try_lock());
  REQUIRE(!lock.try_lock());
  lock.unlock();
  
  lock_guard<decltype(lock)> h(lock);
  REQUIRE(!lock.try_lock());
}

TEST_CASE("works/spinlock/threaded", "Tests that the spinlock works as intended under threads")
{
  boost::spinlock::spinlock<bool> lock;
  boost::spinlock::atomic<size_t> gate(0);
#pragma omp parallel
  {
    ++gate;
  }
  size_t threads=gate;
  for(size_t i=0; i<1000; i++)
  {
    gate.store(threads);
    size_t locked=0;
#pragma omp parallel for reduction(+:locked)
    for(int n=0; n<threads; n++)
    {
      --gate;
      while(gate);
      locked+=lock.try_lock();
    }
    REQUIRE(locked==1);
    lock.unlock();
  }
}

TEST_CASE("works/spinlock/transacted", "Tests that the spinlock works as intended under transactions")
{
  boost::spinlock::spinlock<bool> lock;
  boost::spinlock::atomic<size_t> gate(0);
  size_t locked=0;
#pragma omp parallel
  {
    ++gate;
  }
  size_t threads=gate;
#pragma omp parallel for
  for(int i=0; i<1000*threads; i++)
  {
    BOOST_BEGIN_TRANSACT_LOCK(lock)
    {
      ++locked;
    }
    BOOST_END_TRANSACT_LOCK(lock)
  }
  REQUIRE(locked==1000*threads);
}

static double CalculatePerformance(bool use_transact)
{
  boost::spinlock::spinlock<bool> lock;
  boost::spinlock::atomic<size_t> gate(0);
  struct
  {
    size_t value;
    char padding[64-sizeof(size_t)];
  } count[64];
  memset(&count, 0, sizeof(count));
  usCount start, end;
#pragma omp parallel
  {
    ++gate;
  }
  size_t threads=gate;
  //printf("There are %u threads in this CPU\n", (unsigned) threads);
  start=GetUsCount();
#pragma omp parallel for
  for(int thread=0; thread<threads; thread++)
  {
    --gate;
    while(gate);
    for(size_t n=0; n<10000000; n++)
    {
      if(use_transact)
      {
        BOOST_BEGIN_TRANSACT_LOCK(lock)
        {
          ++count[thread].value;
        }
        BOOST_END_TRANSACT_LOCK(lock)
      }
      else
      {
        std::lock_guard<decltype(lock)> g(lock);
        ++count[thread].value;      
      }
    }
  }
  end=GetUsCount();
  size_t increments=0;
  for(size_t thread=0; thread<threads; thread++)
  {
    REQUIRE(count[thread].value == 10000000);
    increments+=count[thread].value;
  }
  return increments/((end-start)/1000000000000.0);
}

TEST_CASE("performance/spinlock", "Tests the performance of spinlocks")
{
  printf("\n=== Spinlock performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculatePerformance(false));
  printf("2. Achieved %lf transactions per second\n", CalculatePerformance(false));
  printf("3. Achieved %lf transactions per second\n", CalculatePerformance(false));
}

TEST_CASE("performance/transaction", "Tests the performance of spinlock transactions")
{
  printf("\n=== Transacted spinlock performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculatePerformance(true));
  printf("2. Achieved %lf transactions per second\n", CalculatePerformance(true));
  printf("3. Achieved %lf transactions per second\n", CalculatePerformance(true));
}

static double CalculateMallocPerformance(size_t size, bool use_transact)
{
  boost::spinlock::spinlock<bool> lock;
  boost::spinlock::atomic<size_t> gate(0);
  usCount start, end;
#pragma omp parallel
  {
    ++gate;
  }
  size_t threads=gate;
  //printf("There are %u threads in this CPU\n", (unsigned) threads);
  start=GetUsCount();
#pragma omp parallel for
  for(int n=0; n<10000000*threads; n++)
  {
    void *p;
    if(use_transact)
    {
      BOOST_BEGIN_TRANSACT_LOCK(lock)
      {
        p=malloc(size);
      }
      BOOST_END_TRANSACT_LOCK(lock)
    }
    else
    {
      std::lock_guard<decltype(lock)> g(lock);
      p=malloc(size);
    }
    if(use_transact)
    {
      BOOST_BEGIN_TRANSACT_LOCK(lock)
      {
        free(p);
      }
      BOOST_END_TRANSACT_LOCK(lock)
    }
    else
    {
      std::lock_guard<decltype(lock)> g(lock);
      free(p);
    }
  }
  end=GetUsCount();
  REQUIRE(true);
//  printf("size=%u\n", (unsigned) map.size());
  return threads*10000000/((end-start)/1000000000000.0);
}

TEST_CASE("performance/malloc/transact/small", "Tests the transact performance of multiple threads using small memory allocations")
{
  printf("\n=== Small malloc transact performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateMallocPerformance(16, 1));
  printf("2. Achieved %lf transactions per second\n", CalculateMallocPerformance(16, 1));
  printf("3. Achieved %lf transactions per second\n", CalculateMallocPerformance(16, 1));
}

TEST_CASE("performance/malloc/transact/large", "Tests the transact performance of multiple threads using large memory allocations")
{
  printf("\n=== Large malloc transact performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateMallocPerformance(65536, 1));
  printf("2. Achieved %lf transactions per second\n", CalculateMallocPerformance(65536, 1));
  printf("3. Achieved %lf transactions per second\n", CalculateMallocPerformance(65536, 1));
}







TEST_CASE("works/concurrent_unordered_map/basic", "Tests that concurrent_unordered_map works as expected")
{
  printf("\n=== concurrent_unordered_map basic ===\n");
  boost::spinlock::concurrent_unordered_map<int, int> map1, map2;
  map1.reserve(10);    // test dense map
  map2.reserve(1000);  // test sparse map
  CHECK(map1.empty());
  CHECK(map2.empty());
  CHECK(map1.size()==0);
  CHECK(map2.size()==0);
  for(int n=-200; n<=200; n+=2)
  {
    map1.emplace(n, n);
    map2.emplace(n, n);
  }
  CHECK(!map1.empty());
  CHECK(!map2.empty());
  CHECK(map1.size()==201);
  CHECK(map2.size()==201);
  printf("Load factor for map1 is %f\n", map1.load_factor());
  printf("Load factor for map2 is %f\n", map2.load_factor());
  std::vector<std::pair<int, int>> contents1, contents2;
  std::copy(map1.begin(), map1.end(), std::back_inserter(contents1));
  std::copy(map2.begin(), map2.end(), std::back_inserter(contents2));
  CHECK(contents1.size()==201);
  CHECK(contents2.size()==201);
  std::sort(contents1.begin(), contents1.end());
  std::sort(contents2.begin(), contents2.end());
  for(int n=-200; n<=200; n+=2)
  {
    CHECK(contents1[n/2+100].first==n);
    CHECK(contents2[n/2+100].first==n);
  }
  map1.clear();
  map2.clear();
  CHECK(map1.empty());
  CHECK(map2.empty());
  CHECK(map1.size()==0);
  CHECK(map2.size()==0);
}

TEST_CASE("works/concurrent_unordered_map/rehash", "Tests that concurrent_unordered_map rehash works as expected")
{
  printf("\n=== concurrent_unordered_map rehash ===\n");
  boost::spinlock::concurrent_unordered_map<int, int> map1, map2;
  map1.reserve(10);    // test dense map
  map2.reserve(1000);  // test sparse map
  for(int n=-200; n<=200; n+=2)
  {
    map1.emplace(n, n);
    map2.emplace(n, n);
  }
  map1.reserve(1000);
  map2.reserve(10);
  std::vector<std::pair<int, int>> contents1, contents2;
  std::copy(map1.begin(), map1.end(), std::back_inserter(contents1));
  std::copy(map2.begin(), map2.end(), std::back_inserter(contents2));
  CHECK(contents1.size()==201);
  CHECK(contents2.size()==201);
  std::sort(contents1.begin(), contents1.end());
  std::sort(contents2.begin(), contents2.end());
  for(int n=-200; n<=200; n+=2)
  {
    CHECK(contents1[n/2+100].first==n);
    CHECK(contents2[n/2+100].first==n);
  }
}

TEST_CASE("works/concurrent_unordered_map/merge", "Tests that concurrent_unordered_map merge works as expected")
{
  printf("\n=== concurrent_unordered_map merge ===\n");
  boost::spinlock::concurrent_unordered_map<int, int> map1, map2({0});
  CHECK(map1.size()==0);
  CHECK(map2.size()==1);
  map1.reserve(10);    // test dense map
  map2.reserve(1000);  // test sparse map
  for(int n=-200; n<=200; n+=2)
  {
    map1.emplace(n, n);
    map2.emplace(n+1, n);
  }
  CHECK(map1.size()==201);
  CHECK(map2.size()==202);
  map1.merge(map2); // should merge all but 0
  std::vector<std::pair<int, int>> contents1, contents2;
  std::copy(map1.begin(), map1.end(), std::back_inserter(contents1));
  std::copy(map2.begin(), map2.end(), std::back_inserter(contents2));
  CHECK(contents1.size()==402);
  REQUIRE(contents2.size()==1);
  std::sort(contents1.begin(), contents1.end());
  CHECK(contents2[0].first==0);
  for(int n=-200; n<=201; n++)
  {
    CHECK(contents1[n+200].first==n);
  }
}

static double CalculateUnorderedMapPerformance(size_t reserve, bool use_transact, int type)
{
  boost::spinlock::spinlock<bool> lock;
  boost::spinlock::atomic<size_t> gate(0);
  std::unordered_map<int, int> map;
  usCount start, end;
  if(reserve)
  {
    map.reserve(reserve);
    for(int n=0; n<reserve/2; n++)
      map.insert(std::make_pair(-n, n));
  }
#pragma omp parallel
  {
    ++gate;
  }
  size_t threads=gate;
  //printf("There are %u threads in this CPU\n", (unsigned) threads);
  start=GetUsCount();
#pragma omp parallel for
  for(int thread=0; thread<threads; thread++)
  for(int n=0; n<10000000; n++)
  {
    if(2==type)
    {
      // One thread always writes with lock, remaining threads read with transact
      bool amMaster=(thread==0);
      if(amMaster)
      {
        bool doInsert=((n/threads) & 1)!=0;
        std::lock_guard<decltype(lock)> g(lock);
        if(doInsert)
          map.insert(std::make_pair(n, n));
        else if(!map.empty())
          map.erase(map.begin());
      }
      else
      {
        if(use_transact)
        {
          BOOST_BEGIN_TRANSACT_LOCK(lock)
          {
            map.find(n-1);
          }
          BOOST_END_TRANSACT_LOCK(lock)
        }
        else
        {
          std::lock_guard<decltype(lock)> g(lock);
          map.find(n-1);
        }
      }
    }
    else if(1==type)
    {
      if(use_transact)
      {
        int v=-(int)(n % (reserve/2));
        if(v)
        {
          BOOST_BEGIN_TRANSACT_LOCK(lock)
          auto it=map.find(v);
          //if(it==map.end()) std::cout << v;
          BOOST_END_TRANSACT_LOCK(lock)
        }
      }
      else
      {
        int v=-(int)(n % (reserve/2));
        if(v)
        {
          std::lock_guard<decltype(lock)> g(lock);
          auto it=map.find(v);
          if(it==map.end()) std::cout << v;
        }
      }
    }    
    else
    {
      if(use_transact)
      {
#if 0
        size_t v=n*10+thread;
        BOOST_BEGIN_TRANSACT_LOCK(lock)
        {
          if((n & 255)<128)
            map.insert(std::make_pair(v, n));
          else if(!map.empty())
            map.erase(map.find(v-128));
        }
        BOOST_END_TRANSACT_LOCK(lock)
#endif
      }
      else
      {
        size_t v=n*10+thread;
        std::lock_guard<decltype(lock)> g(lock);
        if((n & 255)<128)
          map.insert(std::make_pair(v, n));
        else if(!map.empty())
        {
          auto it=map.find(v-1280);
          if(it!=map.end())
            map.erase(it);
        }
      }
    }
//    if(!(n % 1000000))
//      std::cout << "Items now " << map.size() << std::endl;
  }
  end=GetUsCount();
  REQUIRE(true);
//  printf("size=%u\n", (unsigned) map.size());
  return threads*10000000/((end-start)/1000000000000.0);
}

TEST_CASE("performance/unordered_map/small/write", "Tests the performance of multiple threads writing a small unordered_map")
{
  printf("\n=== Small unordered_map spinlock write performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(0, false, false));
  printf("2. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(0, false, false));
  printf("3. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(0, false, false));
}

TEST_CASE("performance/unordered_map/large/write", "Tests the performance of multiple threads writing a large unordered_map")
{
  printf("\n=== Large unordered_map spinlock write performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, false, false));
  printf("2. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, false, false));
  printf("3. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, false, false));
}

TEST_CASE("performance/unordered_map/large/read", "Tests the performance of multiple threads reading a large unordered_map")
{
  printf("\n=== Large unordered_map spinlock read performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, false, 1));
  printf("2. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, false, 1));
  printf("3. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, false, 1));
}

/*TEST_CASE("performance/unordered_map/transact/small", "Tests the transact performance of multiple threads using a small unordered_map")
{
  printf("\n=== Small unordered_map transact performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(0, true, false));
#ifndef BOOST_HAVE_TRANSACTIONAL_MEMORY_COMPILER
  printf("2. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(0, true, false));
  printf("3. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(0, true, false));
#endif
}

TEST_CASE("performance/unordered_map/transact/large", "Tests the transact performance of multiple threads using a large unordered_map")
{
  printf("\n=== Large unordered_map transact performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, true, false));
#ifndef BOOST_HAVE_TRANSACTIONAL_MEMORY_COMPILER
  printf("2. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, true, false));
  printf("3. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, true, false));
#endif
}*/

static double CalculateConcurrentUnorderedMapPerformance(size_t reserve, int type)
{
  boost::spinlock::atomic<size_t> gate(0);
#ifdef BOOST_HAVE_SYSTEM_CONCURRENT_UNORDERED_MAP
  concurrency::concurrent_unordered_map<int, int> map;
#else
  boost::spinlock::concurrent_unordered_map<int, int> map;
#endif
  usCount start, end;
  if(reserve)
  {
#ifndef BOOST_HAVE_SYSTEM_CONCURRENT_UNORDERED_MAP
    map.reserve(reserve);
#endif
    for(int n=0; n<reserve/2; n++)
      map.insert(std::make_pair(-n, n));
  }
#pragma omp parallel
  {
    ++gate;
  }
  size_t threads=gate;
  printf("There are %u threads in this CPU\n", (unsigned) threads);
  start=GetUsCount();
#pragma omp parallel for
  for(int thread=0; thread<threads; thread++)
  for(int n=0; n<10000000; n++)
  {
#if 0
    if(readwrites)
    {
      // One thread always writes with lock, remaining threads read with transact
      bool amMaster=(thread==0);
      if(amMaster)
      {
        bool doInsert=((n/threads) & 1)!=0;
        if(doInsert)
          map.insert(std::make_pair(n, n));
        else if(!map.empty())
          map.erase(map.begin());
      }
      else
      {
        map.find(n-1);
      }
    }
    else
#endif
    if(0==type)
    {
      size_t v=n*10+thread;
      if((n & 255)<128)
        map.insert(std::make_pair(v, n));
      else
      {
#ifdef BOOST_HAVE_SYSTEM_CONCURRENT_UNORDERED_MAP
        map.unsafe_erase(v-1280);
#else
        map.erase(v-1280);
#endif
      }
    }
    else if(1==type)
    {
      int v=-(int)(n % (reserve/2));
      if(v)
      {
        auto it=map.find(v);
        if(it==map.end()) std::cout << v;
      }
    }
//    if(!(n % 1000000))
//      std::cout << "Items now " << map.size() << std::endl;
  }
  end=GetUsCount();
  //map.dump_buckets(std::cout);
  REQUIRE(true);
//  printf("size=%u\n", (unsigned) map.size());
  return threads*10000000/((end-start)/1000000000000.0);
}

TEST_CASE("performance/concurrent_unordered_map/small", "Tests the performance of multiple threads writing a small concurrent_unordered_map")
{
  printf("\n=== Small concurrent_unordered_map write performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(0, false));
  printf("2. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(0, false));
  printf("3. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(0, false));
#ifdef BOOST_USING_INTEL_TSX
  if(boost::spinlock::intel_stuff::have_intel_tsx_support())
  {
    printf("\nForcing Intel TSX support off ...\n");
    boost::spinlock::intel_stuff::have_intel_tsx_support_result=1;
    printf("1. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(0, false));
    printf("2. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(0, false));
    printf("3. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(0, false));
    boost::spinlock::intel_stuff::have_intel_tsx_support_result=0;
  }
#endif
}

TEST_CASE("performance/concurrent_unordered_map/large/write", "Tests the performance of multiple threads writing a large concurrent_unordered_map")
{
  printf("\n=== Large concurrent_unordered_map write performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, false));
  printf("2. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, false));
  printf("3. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, false));
#ifdef BOOST_USING_INTEL_TSX
  if(boost::spinlock::intel_stuff::have_intel_tsx_support())
  { 
    printf("\nForcing Intel TSX support off ...\n");
    boost::spinlock::intel_stuff::have_intel_tsx_support_result=1;
    printf("1. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, false));
    printf("2. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, false));
    printf("3. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, false));
    boost::spinlock::intel_stuff::have_intel_tsx_support_result=0;
  } 
#endif
}

TEST_CASE("performance/concurrent_unordered_map/large/read", "Tests the performance of multiple threads reading a large concurrent_unordered_map")
{
  printf("\n=== Large concurrent_unordered_map read performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, 1));
  printf("2. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, 1));
  printf("3. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, 1));
#ifdef BOOST_USING_INTEL_TSX
  if(boost::spinlock::intel_stuff::have_intel_tsx_support())
  { 
    printf("\nForcing Intel TSX support off ...\n");
    boost::spinlock::intel_stuff::have_intel_tsx_support_result=1;
    printf("1. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, 1));
    printf("2. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, 1));
    printf("3. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, 1));
    boost::spinlock::intel_stuff::have_intel_tsx_support_result=0;
  } 
#endif
}


#ifndef BOOST_MEMORY_TRANSACTIONS_DISABLE_CATCH
int main(int argc, char *argv[])
{
#ifdef _OPENMP
  printf("These unit tests have been compiled with parallel support. I will use as many threads as CPU cores.\n");
#else
  printf("These unit tests have not been compiled with parallel support and will execute only those which are sequential.\n");
#endif
#ifdef BOOST_HAVE_TRANSACTIONAL_MEMORY_COMPILER
  printf("These unit tests have been compiled using a transactional compiler. I will use __transaction_relaxed.\n");
#else
  printf("These unit tests have not been compiled using a transactional compiler.\n");
#endif
  int result=Catch::Session().run(argc, argv);
  return result;
}
#endif
