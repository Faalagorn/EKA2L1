#include <debugger/renderer/opengl/gl_renderer.h>
#include <debugger/debugger.h>

#include <glad/glad.h>
#include <imgui.h>

namespace eka2l1 {
    static const GLchar *vertex_shader_debugger =
    "#version 330\n"
    "uniform mat4 ProjMtx;\n"
    "in vec2 Position;\n"
    "in vec2 UV;\n"
    "in vec4 Color;\n"
    "out vec2 Frag_UV;\n"
    "out vec4 Frag_Color;\n"
    "void main()\n"
    "{\n"
    "	Frag_UV = UV;\n"
    "	Frag_Color = Color;\n"
    "	gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
    "}\n";

    static const GLchar *fragment_shader_renderer =
    "#version 330\n"
    "uniform sampler2D Texture;\n"
    "in vec2 Frag_UV;\n"
    "in vec4 Frag_Color;\n"
    "out vec4 Out_Color;\n"
    "void main()\n"
    "{\n"
    "	Out_Color = Frag_Color * texture( Texture, Frag_UV.st);\n"
    "}\n";

    void debugger_gl_renderer::init(debugger_ptr &dbg)
    {
        debugger_renderer::init(dbg);
        gladLoadGL();

        shader_handle = glCreateProgram();
        vert_handle = glCreateShader(GL_VERTEX_SHADER);
        frag_handle = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(vert_handle, 1, &vertex_shader_debugger, 0);
        glShaderSource(frag_handle, 1, &fragment_shader_renderer, 0);
        glCompileShader(vert_handle);
        glCompileShader(frag_handle);
        glAttachShader(shader_handle, vert_handle);
        glAttachShader(shader_handle, frag_handle);
        glLinkProgram(shader_handle);

        attrib_loc_tex = glGetUniformLocation(shader_handle, "Texture");
        attrib_loc_proj_matrix = glGetUniformLocation(shader_handle, "ProjMtx");
        attrib_loc_pos = glGetAttribLocation(shader_handle, "Position");
        attrib_loc_uv = glGetAttribLocation(shader_handle, "UV");
        attrib_loc_color = glGetAttribLocation(shader_handle, "Color");

        glGenBuffers(1, &vbo_handle);
        glGenBuffers(1, &elements_handle);

        glGenVertexArrays(1, &vao_handle);
        glBindVertexArray(vao_handle);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_handle);
        glEnableVertexAttribArray(attrib_loc_pos);
        glEnableVertexAttribArray(attrib_loc_uv);
        glEnableVertexAttribArray(attrib_loc_color);

        #define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))
        glVertexAttribPointer(attrib_loc_pos, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (void*)OFFSETOF(ImDrawVert, pos));
        glVertexAttribPointer(attrib_loc_uv, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (void*)OFFSETOF(ImDrawVert, uv));
        glVertexAttribPointer(attrib_loc_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (void*)OFFSETOF(ImDrawVert, col));
        #undef OFFSETOF

        auto &io = ImGui::GetIO();
        unsigned char *pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        glGenTextures(1, &font_texture);
        glBindTexture(GL_TEXTURE_2D, font_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA), width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        io.Fonts->TexID = reinterpret_cast<void*>(static_cast<int64_t>(font_texture));
    }

    void debugger_gl_renderer::deinit()
    {
        // TODO: Cleanup our resources
    }

    struct State
    {
        GLint last_program;
        GLint last_texture;
        GLint last_active_texture;
        GLint last_array_buffer;
        GLint last_element_array_buffer;
        GLint last_vertex_array;
        GLint last_blend_src;
        GLint last_blend_dst;
        GLint last_blend_equation_rgb;
        GLint last_blend_equation_alpha;
        GLint last_viewport[4];
        GLint last_scissor[4];
        GLboolean last_enable_blend;
        GLboolean last_enable_cull_face;
        GLboolean last_enable_depth_test;
        GLboolean last_enable_scissor_test;
    };

    static void saveState(State &state)
    {
        glGetIntegerv(GL_CURRENT_PROGRAM, &state.last_program);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.last_texture);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &state.last_active_texture);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state.last_array_buffer);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &state.last_element_array_buffer);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state.last_vertex_array);
        glGetIntegerv(GL_BLEND_SRC, &state.last_blend_src);
        glGetIntegerv(GL_BLEND_DST, &state.last_blend_dst);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &state.last_blend_equation_rgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &state.last_blend_equation_alpha);
        glGetIntegerv(GL_VIEWPORT, state.last_viewport);
        glGetIntegerv(GL_SCISSOR_BOX, state.last_scissor);
        state.last_enable_blend = glIsEnabled(GL_BLEND);
        state.last_enable_cull_face = glIsEnabled(GL_CULL_FACE);
        state.last_enable_depth_test = glIsEnabled(GL_DEPTH_TEST);
        state.last_enable_scissor_test = glIsEnabled(GL_SCISSOR_TEST);
    }

    static void loadState(State &state)
    {
        glUseProgram(state.last_program);
        glActiveTexture(static_cast<GLenum>(state.last_active_texture));
        glBindTexture(GL_TEXTURE_2D, state.last_texture);
        glBindVertexArray(state.last_vertex_array);
        glBindBuffer(GL_ARRAY_BUFFER, state.last_array_buffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state.last_element_array_buffer);

        glBlendEquationSeparate(static_cast<GLenum>(state.last_blend_equation_rgb),
                                    static_cast<GLenum>(state.last_blend_equation_alpha));

        glBlendFunc(static_cast<GLenum>(state.last_blend_src),
                        static_cast<GLenum>(state.last_blend_dst));

        if (state.last_enable_blend == GL_TRUE) {
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
        }

        if (state.last_enable_cull_face == GL_TRUE) {
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }

        if (state.last_enable_depth_test == GL_TRUE) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }

        if (state.last_enable_scissor_test == GL_TRUE) {
            glEnable(GL_SCISSOR_TEST);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }

        glViewport(state.last_viewport[0],
                        state.last_viewport[1],
                        static_cast<GLsizei>(state.last_viewport[2]),
                        static_cast<GLsizei>(state.last_viewport[3]));

        glScissor(state.last_scissor[0],
                        state.last_scissor[1],
                        state.last_scissor[2],
                        state.last_scissor[3]);
    }

    void debugger_gl_renderer::draw(std::uint32_t width, std::uint32_t height
            , std::uint32_t fb_width, std::uint32_t fb_height) {
        // Draw the imgui ui
        debugger->show_debugger(width, height, fb_width, fb_height);
        ImGui::Render();

        // Scale clip rects
        auto &io = ImGui::GetIO();
        auto drawData = ImGui::GetDrawData();
        auto fbWidth = static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x);
        auto fbHeight = static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y);
        drawData->ScaleClipRects(io.DisplayFramebufferScale);

        // Backup GL state
        auto state = State {};
        saveState(state);

        glClear(GL_COLOR_BUFFER_BIT);

        // Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_SCISSOR_TEST);
        glActiveTexture(GL_TEXTURE0);

        // Setup viewport, orthographic projection matrix
        glViewport(0, 0, static_cast<GLsizei>(fbWidth), (GLsizei)fbHeight);
        const float ortho_projection[4][4] =
        {
            {  2.0f / io.DisplaySize.x, 0.0f,                      0.0f, 0.0f },
            {  0.0f,                    2.0f / -io.DisplaySize.y,  0.0f, 0.0f },
            {  0.0f,                    0.0f,                     -1.0f, 0.0f },
            { -1.0f,                    1.0f,                      0.0f, 1.0f },
        };
        glUseProgram(shader_handle);
        glUniform1i(attrib_loc_tex, 0);
        glUniformMatrix4fv(attrib_loc_proj_matrix, 1, GL_FALSE, &ortho_projection[0][0]);
        glBindVertexArray(vao_handle);

        for (auto n = 0; n < drawData->CmdListsCount; n++) {
            const ImDrawList *cmdList = drawData->CmdLists[n];
            const ImDrawIdx *idxBufOffset = nullptr;

            glBindBuffer(GL_ARRAY_BUFFER, vbo_handle);
            glBufferData(GL_ARRAY_BUFFER,
                            static_cast<GLsizeiptr>(cmdList->VtxBuffer.size() * sizeof(ImDrawVert)),
                            reinterpret_cast<const GLvoid *>(&cmdList->VtxBuffer.front()),
                            GL_STREAM_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elements_handle);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)cmdList->IdxBuffer.size() * sizeof(ImDrawIdx), (GLvoid*)&cmdList->IdxBuffer.front(), GL_STREAM_DRAW);

            for (auto pcmd = cmdList->CmdBuffer.begin(); pcmd != cmdList->CmdBuffer.end(); pcmd++) {
                if (pcmd->UserCallback) {
                    pcmd->UserCallback(cmdList, pcmd);
                } else {
                    glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->TextureId);
                    glScissor((int)pcmd->ClipRect.x, (int)(fbHeight - pcmd->ClipRect.w), (int)(pcmd->ClipRect.z - pcmd->ClipRect.x), (int)(pcmd->ClipRect.w - pcmd->ClipRect.y));
                    glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, idxBufOffset);
                }

                idxBufOffset += pcmd->ElemCount;
            }
        }

        // Restore modified GL state
        loadState(state);
    }
}