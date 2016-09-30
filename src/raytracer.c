#include "goxel.h"
#include "pthread.h"

enum {
    RT_READY = 0,
    RT_RUNNING,
    RT_DONE,
};

struct raytracer
{
    int state;
    int w, h;
    const char *pov_path;
    const char *png_path;
    texture_t *texture;
    pthread_t thread;
};

raytracer_t *raytracer_create(void)
{
    raytracer_t *rt = calloc(1, sizeof(*rt));
    return rt;
}

bool raytracer_is_ready(const raytracer_t *rt)
{
    return rt->state == RT_READY || rt->state == RT_DONE;
}

static void *thread_func(void *args)
{
    raytracer_t *rt = args;
    char cmd[256];

    LOG_D("thread_func start");
    sprintf(cmd, "povray -W%d -H%d +A0.1 -D +UA +O%s %s",
            rt->w, rt->h, rt->png_path, rt->pov_path);
    LOG_D("%s", cmd);
    system(cmd);
    LOG_D("thread_func finished");
    rt->state = RT_DONE;

    return NULL;
}

void raytracer_start(raytracer_t *rt, const mesh_t *mesh,
                     const camera_t *cam, int w, int h)
{
    assert(rt->state != RT_RUNNING);
    rt->w = w;
    rt->h = h;
    rt->state = RT_RUNNING;
    rt->pov_path = "/tmp/out.pov";
    rt->png_path = "/tmp/out.png";

    LOG_D("rt start %d %d", w, h);
    texture_delete(rt->texture);
    rt->texture = NULL;
    // XXX: we should pass the mesh here!
    action_exec2("export_as_pov",
                 ARG("path", rt->pov_path),
                 ARG("width", w),
                 ARG("height", h));
    pthread_create(&rt->thread, NULL, thread_func, rt);
}

void raytracer_stop(raytracer_t *rt)
{
    if (rt->state == RT_DONE) {
        rt->state = RT_READY;
        texture_delete(rt->texture);
        rt->texture = NULL;
    }
}

texture_t *raytracer_get_texture(raytracer_t *rt)
{
    if (rt->state != RT_DONE) return NULL;
    if (!rt->texture)
        rt->texture = texture_new_image(rt->png_path);
    return rt->texture;
}
