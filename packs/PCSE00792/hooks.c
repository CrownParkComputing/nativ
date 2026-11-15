/* DARIUSBURST's host code. A diagnostic for where it stops: libgxm's display-queue thread reads the
 * game's display-queue callback out of its control block (r4 + 0x14) and calls it. Eight passes are
 * right and the ninth has r4 == 0, so the block pointer itself is lost, not the callback word. This
 * reports the thread and the return address with it, to see whether the ninth pass is the same thread
 * reaching the same code another way. MOD_IHOOKS in pack.toml places it. */
#include <stdio.h>
#include "rt.h"

uint32_t recomp_cur_tid(void);
const char *recomp_cur_tname(void);

/* ...and the thread function's own first instruction, to see whether it is entered more than once */
void recomp_ihook_gxm_entry(Cpu *c) {
	static int n;
	fprintf(stderr, "    gxm display queue: ENTERED (%d) cpu %p thread %#x '%s' sp %#x\n",
	        ++n, (void *)c, recomp_cur_tid(), recomp_cur_tname() ? recomp_cur_tname() : "?", c->r[13]);
}

void recomp_ihook_gxm_cb(Cpu *c) {
	static int n;
	if (n++ < 14)
		fprintf(stderr, "    gxm display queue: call %d: cpu %p block %#x callback %#x  (thread %#x '%s', sp %#x)\n",
		        n, (void *)c, c->r[4], c->r[4] ? RD32(c->r[4] + 0x14) : 0, recomp_cur_tid(),
		        recomp_cur_tname() ? recomp_cur_tname() : "?", c->r[13]);
}

/* right after the loop's call to sceKernelSetEventFlag: is the block pointer still good there? */
void recomp_ihook_gxm_loop(Cpu *c) {
	static int n;
	if (n++ < 14) fprintf(stderr, "    gxm loop %d: after SetEventFlag r4 %#x r5 %#x sp %#x\n", n, c->r[4], c->r[5], c->r[13]);
}
