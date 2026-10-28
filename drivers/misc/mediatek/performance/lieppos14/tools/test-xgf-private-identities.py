#!/usr/bin/env python3
"""Run actual private XGF graph/RB/path ownership with modeled PID/ring/alloc APIs.
Full private C translation unit is included; unused unrelated functions are GC'd.
Not a native kernel ABI, allocation-context, ring concurrency or root-identity proof.
"""
import argparse
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=pathlib.Path)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
c = '#define noinline __attribute__((noinline))\n'
c += '#include "' + str(root / 'source/fpsgo-private-recon.c') + '"\n'
c += r'''
/* No libc headers: the reconstructed private TU owns its historical bool and
 * size_t aliases. These prototypes model libc's LP64 ABI only for this test. */
extern void *calloc(size_t, size_t);
extern void free(void *);
extern void abort(void);
extern int printf(const char *, ...);
extern int puts(const char *);
#define CHECK(x) do {if(!(x)) {printf("FAIL line %d: %s\n",__LINE__,#x);abort();}} while(0)
struct pid {int refs;int nr;};
static struct pid render={1,71}, caller={1,23}, caller_new={1,23}, leaf={1,33}, render_new={1,71};
static int heap_live, fail_after=-1;
void *xgf_alloc_array(int num,int size) {
    if(num<=0 || size<=0 || fail_after==0)return (void *)0;
    if(fail_after>0)fail_after--;
    void *p=calloc((size_t)num,(size_t)size);if(p)heap_live++;return p;
}
void *xgf_alloc(int size) {return xgf_alloc_array(1,size);}
void xgf_free(void *p) {if(p){CHECK(heap_live>0);heap_live--;free(p);}}
void xgf_trace(const char *fmt, ...) {}
void xgf_get_runtime(int pid, unsigned long long *loading) {*loading=0;}
struct pid *fpsgo_xgf_pid_ref_get(struct pid *p) {if(p){CHECK(p->refs>0);p->refs++;}return p;}
void fpsgo_xgf_pid_ref_put(struct pid *p) {if(p){CHECK(p->refs>1);p->refs--;}}
void fpsgo_xgf_trace_put(struct fpsgo_trace_event *s) {
    fpsgo_xgf_pid_ref_put(s->pid_ref);fpsgo_xgf_pid_ref_put(s->note_ref);
    s->pid_ref=s->note_ref=(void *)0;
}
static struct fpsgo_trace_event events[30000];
int fpsgo_xgf_trace_read(int buffer,int index,struct fpsgo_trace_event *out) {
    *out=(struct fpsgo_trace_event){0};
    if(buffer || index<0 || index>=30000 || !events[index].sequence)return -2;
    *out=events[index];out->pid_ref=out->note_ref=(void *)0;return 0;
}
int fpsgo_xgf_trace_get(int buffer,int index,unsigned long long expected,struct fpsgo_trace_event *out) {
    *out=(struct fpsgo_trace_event){0};
    if(buffer || index<0 || index>=30000 || !events[index].sequence)return -2;
    if(expected && expected!=events[index].sequence)return -116;
    *out=events[index];out->pid_ref=fpsgo_xgf_pid_ref_get(out->pid_ref);
    out->note_ref=fpsgo_xgf_pid_ref_get(out->note_ref);return 0;
}
static void baseline(void) {
    CHECK(render.refs==1 && caller.refs==1 && caller_new.refs==1 && leaf.refs==1 && render_new.refs==1);
}
static struct xgf_render_info *make_render(void) {
    xgf_max_dep_path_num=2;xgf_max_dep_task_num=4;
    struct xgf_render_info *r=xgf_get_render_info(71,99,71,200,1);
    CHECK(r && r->xrd_capacity==2);r->dep_frames=3;r->frame_count=1;
    r->q2q.start_ts=100;r->q2q.end_ts=200;return r;
}
static int nodes(struct fpsgo_rb_root *tree) {
    int n=0;for(struct fpsgo_rb_node *p=fpsgo_rb_first(tree);p;p=fpsgo_rb_next(p))n++;
    return n;
}
static void destroy(void) {
    xgf_delete_render_info(71,99);CHECK(!xgf_render_infos.first && heap_live==0);baseline();
}
static struct fpsgo_trace_event waking={.event=2,.pid=71,.note=23,.ts=180,.pid_ref=&render,.note_ref=&caller,.sequence=3};
static void graph_build(int wrong_identity) {
    struct xgf_render_info *r=make_render();r->curr_index=3;
    events[3]=(struct fpsgo_trace_event){.ts=220,.sequence=4};
    events[2]=waking;
    events[1]=(struct fpsgo_trace_event){.event=2,.pid=23,.note=33,.ts=150,.pid_ref=wrong_identity?&caller_new:&caller,.note_ref=&leaf,.sequence=2};
    events[0]=(struct fpsgo_trace_event){.event=0,.ts=90,.sequence=1};
    CHECK(!xgf_build_critical_path((void *)0,0,r));
    CHECK(r->xrd_arr_idx==1 && r->xrd_arr[0].xdt_arr_idx==(wrong_identity?2:3));
    CHECK(r->xrd_arr[0].xdt_arr[0].tid_ref==&render);
    CHECK(r->xrd_arr[0].xdt_arr[1].tid_ref==&caller);
    if(!wrong_identity)CHECK(r->xrd_arr[0].xdt_arr[2].tid_ref==&leaf);
    CHECK(nodes(&r->in_dep_list)==(wrong_identity?2:3));
    CHECK(!xgf_duplicate_deps_list(r));CHECK(nodes(&r->out_dep_list)==nodes(&r->in_dep_list));
    CHECK(!xgf_update_dep_list(r));CHECK(nodes(&r->prev_dep_list)==nodes(&r->out_dep_list));
    xgf_max_dep_task_num=1;xgf_max_dep_path_num=1; /* cleanup uses ALLOCATION bounds */
    xgf_render_dep_reset(r);CHECK(!r->xrd_arr_idx && !r->xrd_arr[0].xdt_arr);
    xgf_clean_dep_list(r,0);xgf_clean_dep_list(r,1);xgf_clean_dep_list(r,2);baseline();
    destroy();
}
static void rb_identity(void) {
    struct xgf_render_info *r=make_render();
    CHECK(!xgf_get_dep_rb_node(71,r,0,1,(void *)0));
    struct xgf_dep_rb_node *a=xgf_get_dep_rb_node(71,r,0,1,&render);
    struct xgf_dep_rb_node *b=xgf_get_dep_rb_node(71,r,0,1,&render_new);
    CHECK(a && b && a!=b && a->identity==&render && b->identity==&render_new);
    CHECK(a==xgf_get_dep_rb_node(71,r,0,1,&render) && nodes(&r->in_dep_list)==2);
    CHECK(!xgf_duplicate_deps_list(r));CHECK(nodes(&r->out_dep_list)==2);
    CHECK(!xgf_update_dep_list(r));CHECK(nodes(&r->prev_dep_list)==2);
    int ids[3]={0},count=0;
    xgf_export_deps_list(ids,&count,3,r);CHECK(count==1 && ids[0]==71);
    xgf_export_deps_list(ids,&count,0,r);CHECK(!count);
    /* Expire the previous-frame slot; outgoing entries get fresh owned refs. */
    r->frame_count=0;CHECK(!xgf_update_dep_list(r));CHECK(nodes(&r->prev_dep_list)==2);
    destroy();
}
static void changed_limits(void) {
    struct xgf_render_info *r=make_render();
    CHECK(xgf_render_dep_generate(2,0,r,&waking)==1);
    struct fpsgo_trace_event e=waking;e.pid_ref=&render_new;
    CHECK(xgf_render_dep_generate(2,0,r,&e)==2);
    xgf_max_dep_path_num=50; /* must not index beyond original two slots */
    CHECK(xgf_render_dep_generate(2,0,r,&e)==-12);
    xgf_max_dep_path_num=1;xgf_max_dep_task_num=1;
    xgf_render_dep_reset(r);CHECK(!r->xrd_arr_idx);
    xgf_clean_dep_list(r,0);baseline();destroy();
}
static void destroy_all(void) {
    struct xgf_render_info *r=make_render();
    CHECK(xgf_render_dep_generate(2,0,r,&waking)==1);
    struct xgf_render_info *other=xgf_get_render_info(72,100,71,200,1);
    CHECK(other);other->dep_frames=3;
    CHECK(xgf_get_dep_rb_node(71,other,0,1,&render));
    fpsgo_private_destroy_state();CHECK(!heap_live && !xgf_render_infos.first);baseline();
}
static void errors(void) {
    for(int i=0;i<3;i++) {fail_after=i;CHECK(!xgf_get_render_info(71,99,71,200,1));CHECK(!heap_live && !xgf_render_infos.first);}
    fail_after=-1;
    for(int i=0;i<2;i++) {
        struct xgf_render_info *r=make_render();int before=heap_live;fail_after=i;
        CHECK(xgf_render_dep_generate(2,0,r,&waking)==-12);
        CHECK(heap_live==before && !r->xrd_arr_idx && !r->xrd_arr[0].xdt_arr);baseline();
        fail_after=-1;destroy();
    }
    struct xgf_render_info *r=make_render();
    struct fpsgo_trace_event synthetic=waking;synthetic.pid_ref=synthetic.note_ref=(void *)0;
    CHECK(!xgf_render_dep_generate(2,0,r,&synthetic) && !r->xrd_arr_idx);
    xgf_max_dep_task_num=0;CHECK(xgf_render_dep_generate(2,0,r,&waking)==-22);
    xgf_max_dep_task_num=4;CHECK(xgf_render_dep_generate(2,0,r,&waking)==1);
    r->xrd_arr[0].xdt_arr_idx=0; /* reject before [-1], release still drops refs */
    CHECK(xgf_render_dep_update(1,r,&waking)==-22);
    CHECK(xgf_render_dep_compensate(r)==-22);
    destroy();
    r=make_render();
    CHECK(xgf_get_dep_rb_node(71,r,0,1,&render));
    CHECK(xgf_get_dep_rb_node(23,r,0,1,&caller));
    fail_after=1;CHECK(xgf_duplicate_deps_list(r)==-12);
    CHECK(!nodes(&r->out_dep_list));fail_after=-1;
    CHECK(!xgf_duplicate_deps_list(r));
    fail_after=1;CHECK(xgf_update_dep_list(r)==-12);fail_after=-1;
    destroy(); /* partial previous-tree copy still owns and releases its refs */
    r=make_render();r->dep_frames=0;
    CHECK(xgf_get_dep_rb_node(71,r,0,1,&render));destroy();
    r=make_render();r->curr_index=3;
    events[1].sequence=6; /* overwritten ascending slot in a backwards scan */
    CHECK(xgf_build_critical_path((void *)0,0,r)==-1);destroy();
}
int main(void) {
    graph_build(0);graph_build(1);rb_identity();changed_limits();destroy_all();errors();
    puts("PASS actual private XGF: snapshot refs -> identity-qualified path/RB nodes, reused tids, frame copies/expiry, reset/delete, changed limits, OOM/zero-index/overwrite unwind");
    puts("MODELED PID/ring/allocator APIs; NOT renderer admission, typed exports, downstream FBT identity or native kernel proof");
}
'''
(a.output / 'test.c').write_text(c)
subprocess.run(['gcc','-std=gnu11','-O1','-g',
                '-Wno-builtin-declaration-mismatch', '-Werror=implicit-function-declaration',
                '-fsanitize=address,undefined','-fno-omit-frame-pointer',
                '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
                '-fno-pie','-no-pie',str(a.output/'test.c'),'-o',str(a.output/'test')],check=True)
subprocess.run([str(a.output/'test')],check=True)
