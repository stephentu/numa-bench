#!/usr/bin/env python

import matplotlib
import pylab as plt
import numpy as np

import os
import sys

def make_key(r):
  ap = r[0]['alloc-policy']
  if ap == 'once' or ap == 'per-thread':
    return ap
  assert ap == 'numa'
  pp = r[0]['pin-policy']
  return ap + '-' + pp

if __name__ == '__main__':
  (_, f) = sys.argv
  execfile(f)
  lines = {}
  for r in RESULTS:
    key = make_key(r)
    pts = lines.get(key, {})
    xpt = r[0]['num-cpus']
    ypt = r[1]
    assert xpt not in pts
    pts[xpt] = ypt
    lines[key] = pts

  def mean(x): return sum(x)/len(x)
  def median(x): return x[len(x)/2]

  labels = []
  fig = plt.figure()
  ax  = fig.add_subplot(111)
  for (name, pts) in lines.iteritems():
    spts = sorted(pts.iteritems(), key=lambda x: x[0])
    ypts = [sorted(x[1]) for x in spts]
    ymins = np.array([min(x) for x in ypts])
    ymaxs = np.array([max(x) for x in ypts])
    ymid = np.array([median(x) for x in ypts])
    yerr=np.array([ymid - ymins, ymaxs - ymid])
    xpts = [x[0] for x in spts]
    assert len(xpts)
    print xpts
    print ymid
    ax.errorbar(xpts, ymid, yerr=yerr)
    labels.append(name)

  ax.set_xlabel('num threads')
  ax.set_ylabel('per-core throughput (ops/sec/core)')
  ax.set_ylim(ymin = 0)
  ax.legend(labels)

  bname = '.'.join(os.path.basename(f).split('.')[:-1])
  fig.savefig(bname + '.pdf')
