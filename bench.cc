#include <cstdint>
#include <cassert>
#include <iostream>
#include <sstream>
#include <vector>
#include <thread>
#include <atomic>
#include <random>
#include <mutex>

#include <numa.h>
#include <sched.h>
#include <getopt.h>

#include "timer.hh"

// hardcoded for x86
#define CACHELINE 64

// allocate 512MB per thread- this makes the data not fit in L1/L2/L3 caches.
// for a 30MB L3 cache, only 6% of *each* thread's data can fit completely in
// the cache
#define WORKING_SET_BYTES (512 * (1<<20))

#define NOPS 10000000

using namespace std;

static atomic<int> g_ctr(0); // set by main()
static atomic<bool> g_go(false); // set by main()
static char *g_slab_px = nullptr;
static int g_verbose = 0;

template <typename T>
static string
hexify(const T &t)
{
  ostringstream buf;
  buf << hex << t;
  return buf.str();
}

static vector<int>
range(int n)
{
  vector<int> ret;
  for (int i = 0; i < n; i++)
    ret.push_back(i);
  return ret;
}

// pin to specific cpu
static void
pin_to_cpu(int cpu)
{
  auto ncpus = numa_num_task_cpus();
  assert(CPU_SETSIZE >= ncpus); // lazy
  cpu_set_t cs;
  CPU_ZERO(&cs);
  CPU_SET(cpu, &cs);
  assert(CPU_COUNT(&cs) == 1);
  auto ret = sched_setaffinity(0, sizeof(cs), &cs);
  if (ret) {
    perror("sched_setaffinity");
    assert(false);
  }
  ret = sched_yield();
  if (ret)
    assert(false);
}

// pin to the numa node of the cpu
static void
pin_to_node(int cpu)
{
  auto node = numa_node_of_cpu(cpu);
  // pin to node
  auto ret = numa_run_on_node(node);
  if (ret)
    assert(false);
  // is numa_run_on_node() guaranteed to take effect immediately?
  ret = sched_yield();
  if (ret)
    assert(false);
}

static void *
numa_alloc(size_t bytes, int node)
{
  void *p = numa_alloc_onnode(bytes, node);
  assert(p);
  // force the OS to allocate physical memory for the region
  memset(p, 0, bytes);
  return p;
}

static void *
regular_alloc(size_t bytes, int node)
{
  void *p = malloc(bytes);
  assert(p);
  // force the OS to allocate physical memory for the region
  memset(p, 0, bytes);
  return p;
}

static void *
large_slab_alloc(size_t bytes, int node)
{
  static mutex s_mutex;
  lock_guard<mutex> l(s_mutex);
  // XXX: assumes that the slab is big enough for now
  void *p = g_slab_px;
  g_slab_px += bytes;
  return p;
}

enum {
  CONFIG_PIN_NONE,
  CONFIG_PIN_NODE,
  CONFIG_PIN_CPU,
};

enum {
  CONFIG_ALLOC_ONCE,
  CONFIG_ALLOC_PER_THREAD,
  CONFIG_ALLOC_NUMA,
};

static void
work_main(int PinPolicy, int AllocPolicy, int cpu)
{
  auto node = numa_node_of_cpu(cpu);

  // pin to cpu/node
  switch (PinPolicy) {
  case CONFIG_PIN_NONE:
    break;
  case CONFIG_PIN_NODE:
    pin_to_node(cpu);
    break;
  case CONFIG_PIN_CPU:
    pin_to_cpu(cpu);
    break;
  default:
    assert(false);
    break;
  }

  void *p = nullptr;
  switch (AllocPolicy) {
  case CONFIG_ALLOC_ONCE:
    p = large_slab_alloc(WORKING_SET_BYTES, node);
    break;
  case CONFIG_ALLOC_PER_THREAD:
    p = regular_alloc(WORKING_SET_BYTES, node);
    break;
  case CONFIG_ALLOC_NUMA:
    p = numa_alloc(WORKING_SET_BYTES, node);
    break;
  default:
    assert(false);
    break;
  }

  default_random_engine gen;
  uniform_int_distribution<unsigned> dist(0, WORKING_SET_BYTES/CACHELINE - 1);

  --g_ctr;
  while (!g_go.load())
    ;

  for (unsigned i = 0; i < NOPS; i++) {
    // pick a random cache line in the working set, do some useless RMW work on
    // that cache-line
    auto cl = dist(gen);
    char *px = reinterpret_cast<char *>(p) + cl * CACHELINE;
    for (unsigned j = 0; j < CACHELINE/sizeof(uint64_t); j++, px += sizeof(uint64_t)) {
      uint64_t *u64px = reinterpret_cast<uint64_t *>(px);
      *u64px = *u64px + 1;
    }
  }

  // XXX: p is leaked, but we don't care
}

int
main(int argc, char **argv)
{
  if (numa_available() < 0) {
    cerr << "no numa API" << endl;
    return 1;
  }
  numa_set_strict(1);
  auto ncpus = numa_num_task_cpus();
  assert(ncpus >= 1);

  static int s_num_cpus = 1;
  static int s_pin_policy = CONFIG_PIN_NONE;
  static int s_alloc_policy = CONFIG_ALLOC_ONCE;

  while (1) {
    static struct option long_options[] =
    {
      {"verbose"      , no_argument       , &g_verbose , 1}   ,
      {"num-cpus"     , required_argument , 0          , 'n'} ,
      {"pin-policy"   , required_argument , 0          , 'p'} ,
      {"alloc-policy" , required_argument , 0          , 'a'} ,
      {0, 0, 0, 0}
    };
    int option_index = 0;
    int c = getopt_long(argc, argv, "p:a:", long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 0:
      if (long_options[option_index].flag != 0)
        break;
      abort();
      break;

    case 'n':
      s_num_cpus = strtoul(optarg, nullptr, 10);
      assert(s_num_cpus > 0 && s_num_cpus <= ncpus);
      break;

    case 'p':
      {
        const string sopt = optarg;
        if (sopt == "none")
          s_pin_policy = CONFIG_PIN_NONE;
        else if (sopt == "node")
          s_pin_policy = CONFIG_PIN_NODE;
        else if (sopt == "cpu")
          s_pin_policy = CONFIG_PIN_CPU;
        else
          assert(false);
      }
      break;

    case 'a':
      {
        const string sopt = optarg;
        if (sopt == "once")
          s_alloc_policy = CONFIG_ALLOC_ONCE;
        else if (sopt == "per-thread")
          s_alloc_policy = CONFIG_ALLOC_PER_THREAD;
        else if (sopt == "numa")
          s_alloc_policy = CONFIG_ALLOC_NUMA;
        else
          assert(false);
      }
      break;

    case '?':
      /* getopt_long already printed an error message. */
      exit(1);

    default:
      abort();
    }
  }

  if (g_verbose) {
    cout << "bench parameters:" << endl;
    cout << "  num_cpus: " << s_num_cpus << endl;
    cout << "  pin_policy: " << s_pin_policy << endl;
    cout << "  alloc_policy: " << s_alloc_policy << endl;

    cout << "NUMA system info:" << endl;
    bitmask *bm = numa_bitmask_alloc(ncpus);
    auto nm = numa_max_node();
    cout << "  numa_num_task_cpus(): " << ncpus << endl;
    cout << "  numa_max_node(): " << nm << endl;
    cout << "  numa_pagesize(): " << numa_pagesize() << endl;
    for (auto i : range(nm + 1)) {
      auto nsize = numa_node_size(i, nullptr);
      cout << "  numa_node_size(" << i << "): " << nsize << endl;
    }
    for (auto i : range(nm + 1)) {
      numa_node_to_cpus(i, bm);
      cout << "  numa_node_to_cpus(" << i << "):";
      for (auto c : range(ncpus))
        if (numa_bitmask_isbitset(bm, c))
          cout << " " << c;
      cout << endl;
    }
    numa_bitmask_free(bm);
  }

  // slab init
  if (s_alloc_policy == CONFIG_ALLOC_ONCE) {
    g_slab_px = (char *) malloc(WORKING_SET_BYTES * static_cast<size_t>(ncpus));
    assert(g_slab_px); memset(g_slab_px, 0, WORKING_SET_BYTES * static_cast<size_t>(ncpus));
  }

  g_ctr.store(s_num_cpus);
  vector<thread> thds;
  for (auto i : range(s_num_cpus))
    thds.emplace_back(work_main, s_pin_policy, s_alloc_policy, i);

  while (g_ctr.load())
    ;
  timer t;
  g_go.store(true);
  for (auto &t : thds)
    t.join();

  auto elasped_us = t.lap();
  auto elasped_sec = static_cast<double>(elasped_us)/1000000.0;
  auto throughput_per_core = static_cast<double>(NOPS) / elasped_sec;

  if (g_verbose) {
    cout << "results:" << endl;
    cout << "  elasped_sec        : " << elasped_sec << endl;
    cout << "  throughput_per_core: " << throughput_per_core << " ops/sec/core" << endl;
  } else {
    cout << throughput_per_core << endl;
  }

  return 0;
}
