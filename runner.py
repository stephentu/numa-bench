#!/usr/bin/env python

import itertools
import platform
import subprocess
import sys

NTRIALS=3

def run_configuration(num_cpus, pin_policy, alloc_policy):
  args = [
      './bench',
      '--num-cpus', str(num_cpus),
      '--pin-policy', pin_policy,
      '--alloc-policy', alloc_policy,
  ]
  print >>sys.stderr, '[INFO] running command %s' % str(args)
  p = subprocess.Popen(args, stdin=open('/dev/null', 'r'), stdout=subprocess.PIPE)
  r = p.stdout.read()
  p.wait()
  toks = r.strip().split(' ')
  assert len(toks) == 1
  return float(toks[0])

if __name__ == '__main__':
  (_, outfile) = sys.argv

  # XXX: less hardcoded
  CPUS = [1] + range(8, 81, 8) # for ben

  grids = [
    {
      'num-cpus'     : CPUS,
      'pin-policy'   : ['none'],
      'alloc-policy' : ['once'],
    },
    {
      'num-cpus'     : CPUS,
      'pin-policy'   : ['none'],
      'alloc-policy' : ['per-thread'],
    },
    {
      'num-cpus'     : CPUS,
      'pin-policy'   : ['node', 'cpu'],
      'alloc-policy' : ['numa'],
    },
  ]

  # iterate over all configs
  results = []
  for grid in grids:
    for (num_cpus, pin_policy, alloc_policy) in itertools.product(
        grid['num-cpus'], grid['pin-policy'], grid['alloc-policy']):
      config = {
          'num-cpus'     : num_cpus,
          'pin-policy'   : pin_policy,
          'alloc-policy' : alloc_policy,
      }
      print >>sys.stderr, '[INFO] running config %s' % (str(config))
      values = []
      for _ in range(NTRIALS):
        value = run_configuration(num_cpus, pin_policy, alloc_policy)
        values.append(value)
      results.append((config, values))

    # write results as soon as grid is finished- each time overwrite with
    # more data
    with open(outfile, 'w') as fp:
      print >>fp, 'RESULTS = %s' % (repr(results))
