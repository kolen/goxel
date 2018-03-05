/* Goxel 3D voxels editor
 *
 * copyright (c) 2018 Guillaume Chereau <guillaume@noctua-software.com>
 *
 * Goxel is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.

 * Goxel is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.

 * You should have received a copy of the GNU General Public License along with
 * goxel.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "device/device.h"
#include "render/camera.h"
#include "render/film.h"
#include "render/graph.h"
#include "render/light.h"
#include "render/mesh.h"
#include "render/nodes.h"
#include "render/object.h"
#include "render/session.h"
#include "render/shader.h"
#include "util/util_transform.h"
#include "util/util_foreach.h"

#include <memory> // for make_unique

extern "C" {
#include "goxel.h"
}

// Convenience macro for cycles string creation.
#define S(v) ccl::ustring(v)

static ccl::Session *g_session = NULL;
static ccl::BufferParams g_buffer_params;
static ccl::SessionParams g_session_params;

ccl::Mesh *create_cube(float size)
{
    ccl::Mesh *mesh = new ccl::Mesh();
    mesh->subdivision_type = ccl::Mesh::SUBDIVISION_NONE;

    ccl::vector<ccl::float3> P;
    P.push_back(ccl::make_float3( size * 0.5f,  size * 0.5f, -size * 0.5f));
    P.push_back(ccl::make_float3( size * 0.5f, -size * 0.5f, -size * 0.5f));
    P.push_back(ccl::make_float3(-size * 0.5f, -size * 0.5f, -size * 0.5f));
    P.push_back(ccl::make_float3(-size * 0.5f,  size * 0.5f, -size * 0.5f));
    P.push_back(ccl::make_float3( size * 0.5f,  size * 0.5f,  size * 0.5f));
    P.push_back(ccl::make_float3( size * 0.5f, -size * 0.5f,  size * 0.5f));
    P.push_back(ccl::make_float3(-size * 0.5f, -size * 0.5f,  size * 0.5f));
    P.push_back(ccl::make_float3(-size * 0.5f,  size * 0.5f,  size * 0.5f));

    ccl::vector<size_t> surfaces;
    surfaces.push_back(4);
    surfaces.push_back(4);
    surfaces.push_back(4);
    surfaces.push_back(4);
    surfaces.push_back(4);
    surfaces.push_back(4);

    ccl::vector<int> verts;
    verts.push_back(0);
    verts.push_back(1);
    verts.push_back(2);
    verts.push_back(3);

    verts.push_back(4);
    verts.push_back(7);
    verts.push_back(6);
    verts.push_back(5);

    verts.push_back(0);
    verts.push_back(4);
    verts.push_back(5);
    verts.push_back(1);

    verts.push_back(1);
    verts.push_back(5);
    verts.push_back(6);
    verts.push_back(2);

    verts.push_back(2);
    verts.push_back(6);
    verts.push_back(7);
    verts.push_back(3);

    verts.push_back(4);
    verts.push_back(0);
    verts.push_back(3);
    verts.push_back(7);

    mesh->verts = P;
    mesh->reserve_mesh(8, 12);

    size_t offset = 0;
    for(size_t i = 0; i < surfaces.size(); i++) {
        for(size_t j = 0; j < surfaces[i] - 2; j++) {
            int v0 = verts[offset];
            int v1 = verts[offset + j + 1];
            int v2 = verts[offset + j + 2];
            mesh->add_triangle(v0, v1, v2, 0, false);
        }
        offset += surfaces[i];
    }

    ccl::Attribute *attr;
    ccl::uchar4 *idata;

    attr = mesh->attributes.add(S("Col"), ccl::TypeDesc::TypeColor,
            ccl::ATTR_ELEMENT_CORNER_BYTE);
    idata = attr->data_uchar4();
    for (int i = 0; i < 6 * 6; i++)
        idata[i] = ccl::make_uchar4(128, 128, 0, 255);

    return mesh;
}

static ccl::Shader *create_cube_shader(void)
{
    ccl::Shader *shader = new ccl::Shader();
    shader->name = "cubeShader";
    ccl::ShaderGraph *shaderGraph = new ccl::ShaderGraph();

    const ccl::NodeType *colorNodeType = ccl::NodeType::find(S("attribute"));
    ccl::ShaderNode *colorShaderNode = static_cast<ccl::ShaderNode*>(
            colorNodeType->create(colorNodeType));
    colorShaderNode->name = "colorNode";
    colorShaderNode->set(*colorShaderNode->type->find_input(S("attribute")),
                         S("Col"));
    shaderGraph->add(colorShaderNode);

    const ccl::NodeType *diffuseBSDFNodeType = ccl::NodeType::find(S("diffuse_bsdf"));
    ccl::ShaderNode *diffuseBSDFShaderNode = static_cast<ccl::ShaderNode*>(
            diffuseBSDFNodeType->create(diffuseBSDFNodeType));
    diffuseBSDFShaderNode->name = "diffuseBSDFNode";
    shaderGraph->add(diffuseBSDFShaderNode);

    shaderGraph->connect(
        colorShaderNode->output("Color"),
        diffuseBSDFShaderNode->input("Color")
    );
    shaderGraph->connect(
        diffuseBSDFShaderNode->output("BSDF"),
        shaderGraph->output()->input("Surface")
    );

    shader->set_graph(shaderGraph);
    return shader;
}

static ccl::Shader *create_light_shader(void)
{
    ccl::Shader *shader = new ccl::Shader();
    shader->name = "lightShader";
    ccl::ShaderGraph *shaderGraph = new ccl::ShaderGraph();

    const ccl::NodeType *emissionNodeType = ccl::NodeType::find(S("emission"));
    ccl::ShaderNode *emissionShaderNode = static_cast<ccl::ShaderNode*>(
            emissionNodeType->create(emissionNodeType));
    emissionShaderNode->name = "emissionNode";
    emissionShaderNode->set(
        *emissionShaderNode->type->find_input(S("strength")),
        1000.0f
    );
    emissionShaderNode->set(
        *emissionShaderNode->type->find_input(S("color")),
        ccl::make_float3(0.8, 0.8, 0.8)
    );

    shaderGraph->add(emissionShaderNode);

    shaderGraph->connect(
        emissionShaderNode->output("Emission"),
        shaderGraph->output()->input("Surface")
    );

    shader->set_graph(shaderGraph);
    return shader;
}

static ccl::Mesh *create_mesh(const mesh_t *mesh)
{
    ccl::Mesh *ret = new ccl::Mesh();
    mesh_iterator_t iter;
    int block_pos[3], nb = 0, i, j;
    voxel_vertex_t* vertices;
    ccl::Attribute *attr;

    ret->subdivision_type = ccl::Mesh::SUBDIVISION_NONE;

    iter = mesh_get_iterator(mesh,
            MESH_ITER_BLOCKS | MESH_ITER_INCLUDES_NEIGHBORS);

    vertices = (voxel_vertex_t*)calloc(
            BLOCK_SIZE * BLOCK_SIZE * BLOCK_SIZE * 6 * 4, sizeof(*vertices));
    while (mesh_iter(&iter, block_pos)) {
        nb = mesh_generate_vertices(mesh, block_pos, 0, vertices);
        if (!nb) continue;
        ret->reserve_mesh(nb * 4, nb * 2);
        for (i = 0; i < nb; i++) { // Once per quad.
            for (j = 0; j < 4; j++) {
                ret->add_vertex(ccl::make_float3(
                            vertices[i * 4 + j].pos[0],
                            vertices[i * 4 + j].pos[1],
                            vertices[i * 4 + j].pos[2]));
            }
            ret->add_triangle(i * 4 + 0, i * 4 + 1, i * 4 + 2, 0, false);
            ret->add_triangle(i * 4 + 2, i * 4 + 3, i * 4 + 0, 0, false);
        }
        break; // Only support one block for the moment!!
    }

    // Set color attribute.
    if (nb) {
        attr = ret->attributes.add(S("Col"), ccl::TypeDesc::TypeColor,
                ccl::ATTR_ELEMENT_CORNER_BYTE);
        for (i = 0; i < nb * 6; i++) {
            attr->data_uchar4()[i] = ccl::make_uchar4(
                    vertices[i / 6 * 4].color[0],
                    vertices[i / 6 * 4].color[1],
                    vertices[i / 6 * 4].color[2],
                    vertices[i / 6 * 4].color[3]
            );
        }
    }

    free(vertices);
    return ret;
}

static ccl::Scene *create_scene(void)
{
    ccl::Scene *scene;
    ccl::SceneParams scene_params;
    scene_params.shadingsystem = ccl::SHADINGSYSTEM_OSL;
    // scene_params.shadingsystem = ccl::SHADINGSYSTEM_SVM;

    scene = new ccl::Scene(scene_params, g_session->device);
    scene->camera->width = 256;
    scene->camera->height = 256;
    scene->camera->fov = ccl::radians(45.0);
    scene->camera->type = ccl::CameraType::CAMERA_PERSPECTIVE;
    scene->camera->full_width = scene->camera->width;
    scene->camera->full_height = scene->camera->height;
    scene->film->exposure = 1.0f;

    scene->camera->matrix = ccl::transform_identity()
        * ccl::transform_translate(ccl::make_float3(0.0f, 0.0f, -10.0f));

    ccl::Shader *object_shader = create_cube_shader();
    object_shader->tag_update(scene);
    scene->shaders.push_back(object_shader);

    ccl::Mesh *mesh = create_mesh(goxel->render_mesh);
    mesh->used_shaders.push_back(object_shader);
    scene->meshes.push_back(mesh);
    ccl::Object *object = new ccl::Object();
    object->name = "mesh";
    object->mesh = mesh;
    scene->objects.push_back(object);

    /*
    for (int i = 0; i < 5; i++) {
        ccl::Mesh *mesh = create_cube(1.0);
        mesh->used_shaders.push_back(object_shader);
        scene->meshes.push_back(mesh);

        ccl::Object *object = new ccl::Object();
        object->name = "cube";
        object->mesh = mesh;
        object->tfm = ccl::transform_identity() *
            ccl::transform_translate(ccl::make_float3(i * 1.1, 0, 0));
        scene->objects.push_back(object);
    }
    */

    ccl::Light *light = new ccl::Light();
    /*
    foreach(const ccl::SocketType& socket, ((ccl::Node*)light)->type->inputs) {
        LOG_D("XXX %s", socket.name.c_str());
    }
    */

    light->set(*((ccl::Node*)light)->type->find_input(S("co")),
               ccl::make_float3(0, 0, -3));

    ccl::Shader *light_shader = create_light_shader();
    light_shader->tag_update(scene);
    scene->shaders.push_back(light_shader);
    light->shader = light_shader;
    scene->lights.push_back(light);

    scene->camera->compute_auto_viewplane();
    scene->camera->need_update = true;
    scene->camera->need_device_update = true;
    return scene;
}

void cycles_init(void)
{
    ccl::DeviceType device_type;
    ccl::DeviceInfo device_info;
    ccl::vector<ccl::DeviceInfo>& devices = ccl::Device::available_devices();

    device_type = ccl::Device::type_from_string("CPU");
    for (const ccl::DeviceInfo& device : devices) {
        if (device_type == device.type) {
            device_info = device;
            break;
        }
    }
    g_session_params.progressive = true;
    g_session_params.start_resolution = 64;
    g_session_params.device = device_info;
    g_session_params.samples = 20;
    // session_params.threads = 1;

    g_buffer_params.width = 256;
    g_buffer_params.height = 256;
    g_buffer_params.full_width = 256;
    g_buffer_params.full_height = 256;
}

void cycles_render(void)
{
    static ccl::DeviceDrawParams draw_params = ccl::DeviceDrawParams();
    GL(glViewport(256, 256, 256, 256));
    GL(glMatrixMode(GL_PROJECTION));
    GL(glLoadIdentity());
    GL(glOrtho(0, g_buffer_params.width, 0, g_buffer_params.height, -1, 1));
    GL(glMatrixMode(GL_MODELVIEW));
    GL(glLoadIdentity());
    GL(glUseProgram(0));

    static uint64_t last_mesh_key = 0;
    uint64_t mesh_key = mesh_get_key(goxel->render_mesh);
    if (mesh_key != last_mesh_key) {
        last_mesh_key = mesh_key;
        if (g_session) delete g_session;
        g_session = new ccl::Session(g_session_params);
        g_session->scene = create_scene();
        g_session->reset(g_buffer_params, g_session_params.samples);
        g_session->start();
    }

    if (!g_session) return;

    g_session->draw(g_buffer_params, draw_params);

    std::string status;
    std::string substatus;
    g_session->progress.get_status(status, substatus);
}
