/* SPDX-License-Identifier: GPL-3.0-only
 * Generates tests/nano_arp_table.h from the Nano Freak test bed (cc -I<nano-freak>/dsp gen.c <nano-freak>/dsp/nf_arp.c). */
#include <stdio.h>
#include "nf_arp.h"
static const char *names[] = {"UP","DOWN","UPDOWN","ORDER","RANDOM","PATTERN","ASPLAYED","CONVERGE","DIVERGE","HOLLOW","JEOPARDY","CHORD","BROKEN","ALTERNATING"};
static void run(int pat, int range, int length, int nk, int rc, int type) {
  nf_arp a; nf_arp_init(&a, 7u);
  static const int keysA[3] = {64, 60, 67};
  a.pattern = pat; a.range = range; a.length = length; a.enabled = 1; a.bpm = 120;
  nf_arp_set_root_chord(&a, rc); nf_arp_set_type(&a, type);
  for (int i = 0; i < nk; ++i) nf_arp_key_on(&a, keysA[i], 100);
  printf("{%d,%d,%d,%d,%d,%d,\"", pat, range, length, nk, rc, type);
  const float T = 60.0f / 120.0f * 0.25f;
  for (int s = 0; s < 24; ++s) {
    nfa_event ev[NFA_MAX_EVENTS]; int ne = nf_arp_run(&a, s ? T : 0.0f, ev), n = 0, note = 0;
    for (int i = 0; i < ne; ++i) if (ev[i].on) { if (!n) note = ev[i].note; ++n; }
    printf("%s%d", s ? " " : "", n ? note : 0);
  }
  printf("\"},\n");
}
int main(void) {
  int pats[] = {0,1,2,3,6,7,8,9,10,12,13};
  for (int pi = 0; pi < 11; ++pi) for (int range = 1; range <= 2; ++range) for (int nk = 1; nk <= 3; ++nk) run(pats[pi], range, 16, nk, 0, 0);
  run(0, 1, 5, 3, 0, 0); run(2, 2, 7, 3, 0, 0); run(9, 2, 3, 3, 0, 0); run(10, 1, 4, 3, 0, 0);
  for (int rc = 1; rc <= 8; ++rc) { run(0, 1, 16, 1, rc, 0); run(2, 1, 16, 1, rc, 0); }
  return 0;
}
