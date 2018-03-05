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
#include "render/mesh.h"
#include "render/nodes.h"
#include "render/object.h"
#include "render/session.h"
#include "render/shader.h"
#include "util/util_transform.h"

#include <memory> // for make_unique

extern "C" {
#include "goxel.h"
}

// Convenience macro for cycles string creation.
#define S(v) ccl::ustring(v)

static ccl::Session *g_session = NULL;
static ccl::BufferParams g_buffer_params;

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
        idata[i] = ccl::make_uchar4(128, 255, 0, 255);

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

    const ccl::NodeType *emissionNodeType = ccl::NodeType::find(S("emission"));
    ccl::ShaderNode *emissionShaderNode = static_cast<ccl::ShaderNode*>(
            emissionNodeType->create(emissionNodeType));
    emissionShaderNode->name = "emissionNode";
    emissionShaderNode->set(
        *emissionShaderNode->type->find_input(S("strength")),
        1.0f
    );
    shaderGraph->add(emissionShaderNode);

    shaderGraph->connect(
        colorShaderNode->output("Color"),
        emissionShaderNode->input("Color")
    );
    shaderGraph->connect(
        emissionShaderNode->output("Emission"),
        shaderGraph->output()->input("Surface")
    );

    shader->set_graph(shaderGraph);
    return shader;
}

void cycles_init(void)
{
    ccl::DeviceType device_type;
    ccl::DeviceInfo device_info;
    ccl::SessionParams session_params;
    ccl::vector<ccl::DeviceInfo>& devices = ccl::Device::available_devices();
    ccl::Scene *scene;
    ccl::SceneParams scene_params;

    device_type = ccl::Device::type_from_string("CPU");
    for (const ccl::DeviceInfo& device : devices) {
        if (device_type == device.type) {
            device_info = device;
            break;
        }
    }
    session_params.progressive = true;
    session_params.start_resolution = 64;
    session_params.device = device_info;
    session_params.samples = 20;
    // session_params.threads = 1;

    g_buffer_params.width = 128;
    g_buffer_params.height = 128;
    g_buffer_params.full_width = 128;
    g_buffer_params.full_height = 128;

    g_session = new ccl::Session(session_params);

    scene_params.shadingsystem = ccl::SHADINGSYSTEM_OSL;
    // scene_params.shadingsystem = ccl::SHADINGSYSTEM_SVM;

    scene = new ccl::Scene(scene_params, g_session->device);
    scene->camera->width = 128;
    scene->camera->height = 128;
    scene->camera->fov = ccl::radians(45.0);
    scene->camera->type = ccl::CameraType::CAMERA_PERSPECTIVE;
    scene->camera->full_width = scene->camera->width;
    scene->camera->full_height = scene->camera->height;
    scene->film->exposure = 1.0f;

    scene->camera->matrix = ccl::transform_identity()
        * ccl::transform_translate(ccl::make_float3(0.0f, 0.0f, -3.0f));

    // Add a cube
    ccl::Mesh *mesh = create_cube(1.0);

    ccl::Shader *object_shader = create_cube_shader();
    object_shader->tag_update(scene);
    scene->shaders.push_back(object_shader);
    mesh->used_shaders.push_back(object_shader);
    scene->meshes.push_back(mesh);

    ccl::Object *object = new ccl::Object();
    object->name = "cube";
    object->mesh = mesh;
    object->tfm = ccl::transform_identity() *
        ccl::transform_rotate(40.0f * 3.14f / 180.0f,
                              ccl::make_float3(1.0f, 1.0f, 1.0f));
    scene->objects.push_back(object);

    scene->camera->need_update = true;
    scene->camera->need_device_update = true;

    g_session->scene = scene;
    g_session->reset(g_buffer_params, session_params.samples);
    g_session->start();
}

void cycles_render(void)
{
    static ccl::DeviceDrawParams draw_params = ccl::DeviceDrawParams();
    GL(glViewport(256, 128, 128, 128));
    GL(glMatrixMode(GL_PROJECTION));
    GL(glLoadIdentity());
    GL(glOrtho(0, g_buffer_params.width, 0, g_buffer_params.height, -1, 1));
    GL(glMatrixMode(GL_MODELVIEW));
    GL(glLoadIdentity());
    GL(glUseProgram(0));

    g_session->draw(g_buffer_params, draw_params);

    std::string status;
    std::string substatus;
    g_session->progress.get_status(status, substatus);
}
