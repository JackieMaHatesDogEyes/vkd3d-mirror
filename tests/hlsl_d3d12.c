/*
 * Copyright (C) 2020 Zebediah Figura for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "d3d12_crosstest.h"
#include "vkd3d_common.h"

#ifndef D3DERR_INVALIDCALL
#define D3DERR_INVALIDCALL 0x8876086c
#endif

struct test_options test_options = {0};

#define check_preprocess(a, b, c, d, e) check_preprocess_(__LINE__, a, b, c, d, e)
static void check_preprocess_(int line, const char *source, const D3D_SHADER_MACRO *macros,
        ID3DInclude *include, const char *present, const char *absent)
{
    ID3D10Blob *blob, *errors;
    const char *code;
    SIZE_T size;
    HRESULT hr;

    hr = D3DPreprocess(source, strlen(source), NULL, macros, include, &blob, &errors);
    assert_that_(line)(hr == S_OK, "Failed to preprocess shader, hr %#x.\n", hr);
    if (errors)
    {
        if (vkd3d_test_state.debug_level)
            trace_(line)("%s\n", (char *)ID3D10Blob_GetBufferPointer(errors));
        ID3D10Blob_Release(errors);
    }
    code = ID3D10Blob_GetBufferPointer(blob);
    size = ID3D10Blob_GetBufferSize(blob);
    if (present)
        ok_(line)(vkd3d_memmem(code, size, present, strlen(present)),
                "\"%s\" not found in preprocessed shader.\n", present);
    if (absent)
        ok_(line)(!vkd3d_memmem(code, size, absent, strlen(absent)),
                "\"%s\" found in preprocessed shader.\n", absent);
    ID3D10Blob_Release(blob);
}

static const char test_include_top[] =
    "#include \"file1\"\n"
    "#include < file2 >\n"
    "ARGES\n";

static const char test_include_file1[] =
    "#define BRONTES\n"
    "#include \"file3\"\n"
    "#ifndef BRONTES\n"
    "#define STEROPES\n"
    "#endif";

static const char test_include_file2[] =
    "#ifdef STEROPES\n"
    "#define ARGES pass\n"
    "#undef STEROPES\n"
    "#include < file2 >\n"
    "#endif";

static const char test_include_file3[] =
    "#undef BRONTES";

static const char test_include2_top[] =
    "#define FUNC(a, b) a ## b\n"
    "#include \"file4\"\n"
    ",ss)";

static const char test_include2_file4[] =
    "FUNC(pa";

static unsigned int refcount_file1, refcount_file2, refcount_file3, include_count_file2;

static HRESULT WINAPI test_include_Open(ID3DInclude *iface, D3D_INCLUDE_TYPE type,
        const char *filename, const void *parent_data, const void **code, UINT *size)
{
    ok(!*code, "Data pointer should be zeroed.\n");
    ok(!*size, "Size pointer should be zeroed.\n");
    if (!strcmp(filename, "file1"))
    {
        ok(type == D3D_INCLUDE_LOCAL, "Got type %#x.\n", type);
        ok(!parent_data, "Got parent data %p.\n", parent_data);
        *code = test_include_file1;
        *size = strlen(test_include_file1);
        ++refcount_file1;
    }
    else if (!strcmp(filename, " file2 "))
    {
        ok(type == D3D_INCLUDE_SYSTEM, "Got type %#x.\n", type);
        if (!include_count_file2++)
            ok(!parent_data, "Got parent data %p.\n", parent_data);
        else
            ok(parent_data == test_include_file2, "Got parent data %p.\n", parent_data);
        *code = test_include_file2;
        *size = strlen(test_include_file2);
        ++refcount_file2;
    }
    else if (!strcmp(filename, "file3"))
    {
        ok(type == D3D_INCLUDE_LOCAL, "Got type %#x.\n", type);
        ok(parent_data == test_include_file1, "Got parent data %p.\n", parent_data);
        *code = test_include_file3;
        *size = strlen(test_include_file3);
        ++refcount_file3;
    }
    else if (!strcmp(filename, "file4"))
    {
        ok(type == D3D_INCLUDE_LOCAL, "Got type %#x.\n", type);
        ok(!parent_data, "Got parent data %p.\n", parent_data);
        *code = test_include2_file4;
        *size = strlen(test_include2_file4);
    }
    else
    {
        ok(0, "Unexpected filename \"%s\".\n", filename);
    }
    return S_FALSE;
}

static HRESULT WINAPI test_include_Close(ID3DInclude *iface, const void *code)
{
    if (code == test_include_file1)
        --refcount_file1;
    else if (code == test_include_file2)
        --refcount_file2;
    else if (code == test_include_file3)
        --refcount_file3;
    return E_FAIL;
}

static const struct ID3DIncludeVtbl test_include_vtbl =
{
    test_include_Open,
    test_include_Close,
};

static HRESULT WINAPI test_include_fail_Open(ID3DInclude *iface, D3D_INCLUDE_TYPE type,
        const char *filename, const void *parent_data, const void **code, UINT *size)
{
    return 0xdeadbeef;
}

static HRESULT WINAPI test_include_fail_Close(ID3DInclude *iface, const void *code)
{
    ok(0, "Unexpected call.\n");
    return E_FAIL;
}

static const struct ID3DIncludeVtbl test_include_fail_vtbl =
{
    test_include_fail_Open,
    test_include_fail_Close,
};

static void test_preprocess(void)
{
    ID3DInclude test_include_fail = {&test_include_fail_vtbl};
    ID3DInclude test_include = {&test_include_vtbl};
    D3D_SHADER_MACRO macros[3];
    ID3D10Blob *blob, *errors;
    unsigned int i;
    HRESULT hr;

    static const struct
    {
        const char *source;
        const char *present;
        const char *absent;
    }
    tests[] =
    {
        /* Stringification. */
        {
            "#define KEY(a) #a\n"
            "KEY(apple)",

            "\"apple\"",
        },
        {
            "#define KEY(a) # a\n"
            "KEY(apple)",

            "\"apple\"",
        },
        {
            "#define KEY(a) #a\n"
            "KEY( \t\r\n apple \t\r\n )",

            "\"apple\"",
        },
        {
            "#define KEY(if) #if\n"
            "KEY(apple)",

            "\"apple\"",
        },
        {
            "#define KEY(a) #a\n"
            "KEY(\"apple\")",

            "\"\\\"apple\\\"\"",
        },
        {
            "#define KEY(a) #b\n"
            "KEY(apple)",

            "\"b\"",
        },
        {
            "#define KEY(a) a\n"
            "KEY(banana #apple)",

            "#",
            "\"apple\"",
        },
        {
            "#define KEY #apple\n"
            "KEY",

            "apple",
            "\"apple\"",
        },
        {
            "banana #apple\n",

            "apple",
            "\"apple\"",
        },
        {
            "banana #apple\n",

            "#",
        },
        {
            "#define KEY2(x) x\n"
            "#define KEY(a) KEY2(#a)\n"
            "KEY(apple)",

            "\"apple\"",
        },
        {
            "#define KEY2(x) #x\n"
            "#define KEY(a) KEY2(#a)\n"
            "KEY(apple)",

            "\"\\\"apple\\\"\"",
        },
        {
            "#define KEY2(x) #x\n"
            "#define KEY(a) KEY2(#x)\n"
            "KEY(apple)",

            "\"\\\"x\\\"\"",
        },

        /* #pragma is preserved. */
        {
            "#pragma pack_matrix(column_major)\n"
            "text",

            "#pragma pack_matrix(column_major)\n",
        },

        /* DOS-style newlines. */
        {
            "#define KEY(a, b) \\\r\n"
            "        a ## b\r\n"
            "KEY(pa,\r\n"
            "ss\r\n"
            ")\r\n"
            "#ifndef KEY\r\n"
            "fail\r\n"
            "#endif\r\n",

            "pass",
            "fail",
        },
        {
            "#define KEY(a, b) \\\r\n"
            "        a ## b\n"
            "KEY(pa,\r\n"
            "ss\n"
            ")\r\n"
            "#ifndef KEY\n"
            "fail\r\n"
            "#endif\n",

            "pass",
            "fail",
        },

        /* Pre-defined macros. */
        {
            "__LINE__",
            "1",
            "__LINE__",
        },
        {
            "\n"
            "__LINE__",
            "2",
            "__LINE__",
        },
        {
            "#define KEY __LINE__\n"
            "KEY",
            "2",
        },

        /* Tokens which must be preserved verbatim for HLSL (i.e. which cannot
         * be broken up with spaces). */
        {"<<", "<<"},
        {">>", ">>"},
        {"++", "++"},
        {"--", "--"},
        {"+=", "+="},
        {"-=", "-="},
        {"*=", "*="},
        {"/=", "/="},
        {"%=", "%="},
        {"&=", "&="},
        {"|=", "|="},
        {"^=", "^="},
        {"<<=", "<<="},
        {">>=", ">>="},
        {"0.0", "0.0"},
        {".0", ".0"},
        {"0.", "0."},
        {"1e1", "1e1"},
        {"1E1", "1E1"},
        {"1e+1", "1e+1"},
        {"1e-1", "1e-1"},
        {".0f", ".0f"},
        {".0F", ".0F"},
        {".0h", ".0h"},
        {".0H", ".0H"},
        {"0.f", "0.f"},
        {"1.1e-1f", "1.1e-1f"},

        /* Parentheses are emitted for object-like macros invoked like
         * function-like macros. */
        {
            "#define KEY value\n"
            "KEY(apple)",

            "(",
        },

        /* Function-like macro with no parentheses and no following tokens (a
         * corner case in our implementation). */
        {
            "#define pass(a) fail\n"
            "pass",

            "pass",
            "fail",
        },

        /* A single-line comment not terminated by a newline. */
        {
            "pass // fail",

            "pass",
            "fail",
        },
    };

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        vkd3d_test_push_context("Source \"%s\"", tests[i].source);
        check_preprocess(tests[i].source, NULL, NULL, tests[i].present, tests[i].absent);
        vkd3d_test_pop_context();
    }

    macros[0].Name = "KEY";
    macros[0].Definition = "value";
    macros[1].Name = NULL;
    macros[1].Definition = NULL;
    check_preprocess("KEY", macros, NULL, "value", "KEY");

    check_preprocess("#undef KEY\nKEY", macros, NULL, "KEY", "value");

    macros[0].Name = NULL;
    check_preprocess("KEY", macros, NULL, "KEY", "value");

    macros[0].Name = "KEY";
    macros[0].Definition = NULL;
    check_preprocess("KEY", macros, NULL, NULL, "KEY");

    macros[0].Name = "0";
    macros[0].Definition = "value";
    check_preprocess("0", macros, NULL, "0", "value");

    macros[0].Name = "KEY(a)";
    macros[0].Definition = "value";
    check_preprocess("KEY(a)", macros, NULL, "KEY", "value");

    macros[0].Name = "KEY";
    macros[0].Definition = "value1";
    macros[1].Name = "KEY";
    macros[1].Definition = "value2";
    macros[2].Name = NULL;
    macros[2].Definition = NULL;
    check_preprocess("KEY", macros, NULL, "value2", NULL);

    macros[0].Name = "KEY";
    macros[0].Definition = "KEY2";
    macros[1].Name = "KEY2";
    macros[1].Definition = "value";
    check_preprocess("KEY", macros, NULL, "value", NULL);

    macros[0].Name = "KEY2";
    macros[0].Definition = "value";
    macros[1].Name = "KEY";
    macros[1].Definition = "KEY2";
    check_preprocess("KEY", macros, NULL, "value", NULL);

    check_preprocess(test_include_top, NULL, &test_include, "pass", "fail");
    ok(!refcount_file1, "Got %d references to file1.\n", refcount_file1);
    ok(!refcount_file2, "Got %d references to file1.\n", refcount_file2);
    ok(!refcount_file3, "Got %d references to file1.\n", refcount_file3);
    ok(include_count_file2 == 2, "file2 was included %u times.\n", include_count_file2);

    /* Macro invocation spread across multiple files. */
    check_preprocess(test_include2_top, NULL, &test_include, "pass", NULL);

    blob = errors = (ID3D10Blob *)0xdeadbeef;
    hr = D3DPreprocess(test_include_top, strlen(test_include_top), NULL, NULL, &test_include_fail, &blob, &errors);
    ok(hr == E_FAIL, "Got hr %#x.\n", hr);
    ok(blob == (ID3D10Blob *)0xdeadbeef, "Expected no compiled shader blob.\n");
    ok(!!errors, "Expected non-NULL error blob.\n");
    if (vkd3d_test_state.debug_level)
        trace("%s\n", (char *)ID3D10Blob_GetBufferPointer(errors));
    ID3D10Blob_Release(errors);
}

#define compile_shader(a, b) compile_shader_(__LINE__, a, b, 0)
#define compile_shader_flags(a, b, c) compile_shader_(__LINE__, a, b, c)
static ID3D10Blob *compile_shader_(unsigned int line, const char *source, const char *target, UINT flags)
{
    ID3D10Blob *blob = NULL, *errors = NULL;
    HRESULT hr;

    hr = D3DCompile(source, strlen(source), NULL, NULL, NULL, "main", target, flags, 0, &blob, &errors);
    ok_(line)(hr == S_OK, "Failed to compile shader, hr %#x.\n", hr);
    if (errors)
    {
        if (vkd3d_test_state.debug_level)
            trace_(line)("%s\n", (char *)ID3D10Blob_GetBufferPointer(errors));
        ID3D10Blob_Release(errors);
    }
    return blob;
}

static void test_thread_id(void)
{
    ID3D12GraphicsCommandList *command_list;
    struct d3d12_resource_readback rb;
    struct test_context context;
    ID3D12Resource *textures[3];
    ID3D12DescriptorHeap *heap;
    unsigned int i, x, y, z;
    ID3D12Device *device;
    ID3D10Blob *cs_code;
    HRESULT hr;

    static const char cs_source[] =
        "RWTexture3D<uint4> group_uav, thread_uav, dispatch_uav;\n"
        "[numthreads(5, 3, 2)]\n"
        "void main(uint3 group : sv_groupid, uint3 thread : sv_groupthreadid, uint3 dispatch : sv_dispatchthreadid)\n"
        "{\n"
        "    group_uav[dispatch] = uint4(group, 1);\n"
        "    thread_uav[dispatch] = uint4(thread, 2);\n"
        "    dispatch_uav[dispatch] = uint4(dispatch, 3);\n"
        "}";

    static const D3D12_DESCRIPTOR_RANGE descriptor_range = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0, 0, 0};

    static const D3D12_ROOT_PARAMETER root_parameter =
    {
        .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
        .DescriptorTable = {1, &descriptor_range},
    };

    static const D3D12_ROOT_SIGNATURE_DESC root_signature_desc =
    {
        .NumParameters = 1,
        .pParameters = &root_parameter,
    };

    if (!init_compute_test_context(&context))
        return;
    command_list = context.list;
    device = context.device;

    hr = create_root_signature(device, &root_signature_desc, &context.root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3);

    for (i = 0; i < 3; ++i)
    {
        textures[i] = create_default_texture3d(device, 16, 8, 8, 1, DXGI_FORMAT_R32G32B32A32_UINT,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12Device_CreateUnorderedAccessView(device, textures[i], NULL, NULL,
                get_cpu_descriptor_handle(&context, heap, i));
    }

    cs_code = compile_shader(cs_source, "cs_5_0");
    context.pipeline_state = create_compute_pipeline_state(device, context.root_signature,
            shader_bytecode(ID3D10Blob_GetBufferPointer(cs_code), ID3D10Blob_GetBufferSize(cs_code)));

    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list,
            0, get_gpu_descriptor_handle(&context, heap, 0));
    ID3D12GraphicsCommandList_Dispatch(command_list, 2, 2, 2);

    transition_resource_state(command_list, textures[0],
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_resource_readback_with_command_list(textures[0], 0, &rb, context.queue, command_list);
    for (x = 0; x < 16; ++x)
    {
        for (y = 0; y < 8; ++y)
        {
            for (z = 0; z < 8; ++z)
            {
                const struct uvec4 *v = get_readback_data(&rb.rb, x, y, z, sizeof(struct uvec4));
                struct uvec4 expect = {x / 5, y / 3, z / 2, 1};

                if (x >= 10 || y >= 6 || z >= 4)
                    memset(&expect, 0, sizeof(expect));

                ok(compare_uvec4(v, &expect), "Got {%u, %u, %u, %u} at (%u, %u, %u).\n",
                        v->x, v->y, v->z, v->w, x, y, z);
            }
        }
    }
    release_resource_readback(&rb);
    reset_command_list(command_list, context.allocator);

    transition_resource_state(command_list, textures[1],
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_resource_readback_with_command_list(textures[1], 0, &rb, context.queue, command_list);
    for (x = 0; x < 16; ++x)
    {
        for (y = 0; y < 8; ++y)
        {
            for (z = 0; z < 8; ++z)
            {
                const struct uvec4 *v = get_readback_data(&rb.rb, x, y, z, sizeof(struct uvec4));
                struct uvec4 expect = {x % 5, y % 3, z % 2, 2};

                if (x >= 10 || y >= 6 || z >= 4)
                    memset(&expect, 0, sizeof(expect));

                ok(compare_uvec4(v, &expect), "Got {%u, %u, %u, %u} at (%u, %u, %u).\n",
                        v->x, v->y, v->z, v->w, x, y, z);
            }
        }
    }
    release_resource_readback(&rb);
    reset_command_list(command_list, context.allocator);

    transition_resource_state(command_list, textures[2],
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_resource_readback_with_command_list(textures[2], 0, &rb, context.queue, command_list);
    for (x = 0; x < 16; ++x)
    {
        for (y = 0; y < 8; ++y)
        {
            for (z = 0; z < 8; ++z)
            {
                const struct uvec4 *v = get_readback_data(&rb.rb, x, y, z, sizeof(struct uvec4));
                struct uvec4 expect = {x, y, z, 3};

                if (x >= 10 || y >= 6 || z >= 4)
                    memset(&expect, 0, sizeof(expect));

                ok(compare_uvec4(v, &expect), "Got {%u, %u, %u, %u} at (%u, %u, %u).\n",
                        v->x, v->y, v->z, v->w, x, y, z);
            }
        }
    }
    release_resource_readback(&rb);
    reset_command_list(command_list, context.allocator);

    for (i = 0; i < 3; ++i)
        ID3D12Resource_Release(textures[i]);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

static void test_create_blob(void)
{
    unsigned int refcount;
    ID3D10Blob *blob;
    HRESULT hr;

    hr = D3DCreateBlob(1, NULL);
    ok(hr == D3DERR_INVALIDCALL, "Got hr %#x.\n", hr);

    hr = D3DCreateBlob(0, NULL);
    ok(hr == D3DERR_INVALIDCALL, "Got hr %#x.\n", hr);

    hr = D3DCreateBlob(0, &blob);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    refcount = ID3D10Blob_Release(blob);
    ok(!refcount, "Got refcount %u.\n", refcount);
}

static void test_get_blob_part(void)
{
    static uint32_t test_blob_part[] =
    {
        /* test_blob_part - fxc.exe /E VS /Tvs_4_0_level_9_0 /Fx */
#if 0
        float4 VS(float4 position : POSITION, float4 pos : SV_POSITION) : SV_POSITION
        {
          return position;
        }
#endif
        0x43425844, 0x0ef2a70f, 0x6a548011, 0x91ff9409, 0x9973a43d, 0x00000001, 0x000002e0, 0x00000008,
        0x00000040, 0x0000008c, 0x000000d8, 0x0000013c, 0x00000180, 0x000001fc, 0x00000254, 0x000002ac,
        0x53414e58, 0x00000044, 0x00000044, 0xfffe0200, 0x00000020, 0x00000024, 0x00240000, 0x00240000,
        0x00240000, 0x00240000, 0x00240000, 0xfffe0200, 0x0200001f, 0x80000005, 0x900f0000, 0x02000001,
        0xc00f0000, 0x80e40000, 0x0000ffff, 0x50414e58, 0x00000044, 0x00000044, 0xfffe0200, 0x00000020,
        0x00000024, 0x00240000, 0x00240000, 0x00240000, 0x00240000, 0x00240000, 0xfffe0200, 0x0200001f,
        0x80000005, 0x900f0000, 0x02000001, 0xc00f0000, 0x80e40000, 0x0000ffff, 0x396e6f41, 0x0000005c,
        0x0000005c, 0xfffe0200, 0x00000034, 0x00000028, 0x00240000, 0x00240000, 0x00240000, 0x00240000,
        0x00240001, 0x00000000, 0xfffe0200, 0x0200001f, 0x80000005, 0x900f0000, 0x04000004, 0xc0030000,
        0x90ff0000, 0xa0e40000, 0x90e40000, 0x02000001, 0xc00c0000, 0x90e40000, 0x0000ffff, 0x52444853,
        0x0000003c, 0x00010040, 0x0000000f, 0x0300005f, 0x001010f2, 0x00000000, 0x04000067, 0x001020f2,
        0x00000000, 0x00000001, 0x05000036, 0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x0100003e,
        0x54415453, 0x00000074, 0x00000002, 0x00000000, 0x00000000, 0x00000002, 0x00000000, 0x00000000,
        0x00000000, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x46454452,
        0x00000050, 0x00000000, 0x00000000, 0x00000000, 0x0000001c, 0xfffe0400, 0x00000100, 0x0000001c,
        0x7263694d, 0x666f736f, 0x52282074, 0x4c482029, 0x53204c53, 0x65646168, 0x6f432072, 0x6c69706d,
        0x39207265, 0x2e39322e, 0x2e323539, 0x31313133, 0xababab00, 0x4e475349, 0x00000050, 0x00000002,
        0x00000008, 0x00000038, 0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x00000041,
        0x00000000, 0x00000000, 0x00000003, 0x00000001, 0x0000000f, 0x49534f50, 0x4e4f4954, 0x5f565300,
        0x49534f50, 0x4e4f4954, 0xababab00, 0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49,
    };

    static uint32_t test_blob_part2[] =
    {
        /* test_blob_part2 - fxc.exe /E BHS /Ths_5_0 /Zi */
#if 0
        struct VSO { float3 p : POSITION; };
        struct HSDO { float e[4] : SV_TessFactor; float i[2] : SV_InsideTessFactor; };
        struct HSO { float3 p : BEZIERPOS; };
        HSDO BCHS( InputPatch<VSO, 8> ip, uint PatchID : SV_PrimitiveID )
        {
            HSDO res;
            res.e[0] = res.e[1] = res.e[2] = res.e[3] = 1.0f;
            res.i[0] = res.i[1] = 1.0f;
            return res;
        }
        [domain("quad")]
        [partitioning("integer")]
        [outputtopology("triangle_cw")]
        [outputcontrolpoints(8)]
        [patchconstantfunc("BCHS")]
        HSO BHS( InputPatch<VSO, 8> p, uint i : SV_OutputControlPointID, uint PatchID : SV_PrimitiveID )
        {
            HSO res;
            res.p = p[i].p;
            return res;
        }
#endif
        0x43425844, 0xa9d455ae, 0x4cf9c0df, 0x4cf806dc, 0xc57a8c2c, 0x00000001, 0x0000139b, 0x00000007,
        0x0000003c, 0x000000b4, 0x000000e8, 0x0000011c, 0x000001e0, 0x00000320, 0x000003bc, 0x46454452,
        0x00000070, 0x00000000, 0x00000000, 0x00000000, 0x0000003c, 0x48530500, 0x00000101, 0x0000003c,
        0x31314452, 0x0000003c, 0x00000018, 0x00000020, 0x00000028, 0x00000024, 0x0000000c, 0x00000000,
        0x7263694d, 0x666f736f, 0x52282074, 0x4c482029, 0x53204c53, 0x65646168, 0x6f432072, 0x6c69706d,
        0x39207265, 0x2e39322e, 0x2e323539, 0x31313133, 0xababab00, 0x4e475349, 0x0000002c, 0x00000001,
        0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000707, 0x49534f50,
        0x4e4f4954, 0xababab00, 0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000,
        0x00000000, 0x00000003, 0x00000000, 0x00000807, 0x495a4542, 0x4f505245, 0xabab0053, 0x47534350,
        0x000000bc, 0x00000006, 0x00000008, 0x00000098, 0x00000000, 0x0000000b, 0x00000003, 0x00000000,
        0x00000e01, 0x00000098, 0x00000001, 0x0000000b, 0x00000003, 0x00000001, 0x00000e01, 0x00000098,
        0x00000002, 0x0000000b, 0x00000003, 0x00000002, 0x00000e01, 0x00000098, 0x00000003, 0x0000000b,
        0x00000003, 0x00000003, 0x00000e01, 0x000000a6, 0x00000000, 0x0000000c, 0x00000003, 0x00000004,
        0x00000e01, 0x000000a6, 0x00000001, 0x0000000c, 0x00000003, 0x00000005, 0x00000e01, 0x545f5653,
        0x46737365, 0x6f746361, 0x56530072, 0x736e495f, 0x54656469, 0x46737365, 0x6f746361, 0xabab0072,
        0x58454853, 0x00000138, 0x00030050, 0x0000004e, 0x01000071, 0x01004093, 0x01004094, 0x01001895,
        0x01000896, 0x01001897, 0x0100086a, 0x01000073, 0x02000099, 0x00000004, 0x0200005f, 0x00017000,
        0x04000067, 0x00102012, 0x00000000, 0x0000000b, 0x04000067, 0x00102012, 0x00000001, 0x0000000c,
        0x04000067, 0x00102012, 0x00000002, 0x0000000d, 0x04000067, 0x00102012, 0x00000003, 0x0000000e,
        0x02000068, 0x00000001, 0x0400005b, 0x00102012, 0x00000000, 0x00000004, 0x04000036, 0x00100012,
        0x00000000, 0x0001700a, 0x06000036, 0x00902012, 0x0010000a, 0x00000000, 0x00004001, 0x3f800000,
        0x0100003e, 0x01000073, 0x02000099, 0x00000002, 0x0200005f, 0x00017000, 0x04000067, 0x00102012,
        0x00000004, 0x0000000f, 0x04000067, 0x00102012, 0x00000005, 0x00000010, 0x02000068, 0x00000001,
        0x0400005b, 0x00102012, 0x00000004, 0x00000002, 0x04000036, 0x00100012, 0x00000000, 0x0001700a,
        0x07000036, 0x00d02012, 0x00000004, 0x0010000a, 0x00000000, 0x00004001, 0x3f800000, 0x0100003e,
        0x54415453, 0x00000094, 0x00000006, 0x00000001, 0x00000000, 0x00000005, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x0000000f, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000008, 0x00000003, 0x00000001, 0x00000003, 0x00000000, 0x00000000, 0x00000000, 0x47424453,
        0x00000fd7, 0x00000054, 0x000002d5, 0x00000306, 0x0000030a, 0x00000101, 0x00000001, 0x00000000,
        0x00000006, 0x00000010, 0x00000006, 0x00000958, 0x00000000, 0x000009e8, 0x00000008, 0x000009e8,
        0x00000006, 0x00000a88, 0x00000007, 0x00000b00, 0x00000c34, 0x00000c64, 0x00000000, 0x0000004a,
        0x0000004a, 0x0000026a, 0x00000000, 0x00000036, 0x00000001, 0x00000004, 0x00000000, 0xffffffff,
        0x00000000, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0x00000000, 0x00000003, 0x00000000,
        0x00000003, 0x80000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0xffffffff, 0xffffffff, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000007,
        0x00000000, 0x00000003, 0x00000024, 0x00000000, 0x00000000, 0x00000001, 0x00000036, 0x00000001,
        0x00000001, 0x00000000, 0xffffffff, 0x00000000, 0xffffffff, 0xffffffff, 0xffffffff, 0x00000000,
        0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000, 0x00000000, 0x00000000,
        0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000007, 0x00000000, 0x00000003, 0x00000024, 0x00000000, 0x00000000,
        0x00000002, 0x0000003e, 0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000007, 0x00000000, 0x00000003,
        0x00000024, 0x00000000, 0x00000000, 0x00000003, 0x00000036, 0x00000001, 0x00000004, 0x00000000,
        0xffffffff, 0x00000000, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0x00000000, 0x00000001,
        0x00000000, 0x00000001, 0x80000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0xffffffff, 0xffffffff, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000007, 0x00000000, 0x00000003, 0x00000024, 0x00000000, 0x00000000, 0x00000004, 0x00000036,
        0x00000001, 0x00000001, 0x00000004, 0xffffffff, 0x00000000, 0xffffffff, 0xffffffff, 0xffffffff,
        0x00000004, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000, 0x00000000,
        0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000007, 0x00000000, 0x00000003, 0x00000024, 0x00000000,
        0x00000000, 0x00000005, 0x0000003e, 0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000007, 0x00000000,
        0x00000003, 0x00000024, 0x00000000, 0x00000000, 0x00000006, 0x00000003, 0xffffffff, 0xffffffff,
        0x00000003, 0x00000000, 0x00000006, 0x00000003, 0xffffffff, 0xffffffff, 0x00000003, 0x00000001,
        0x00000006, 0x00000003, 0xffffffff, 0xffffffff, 0x00000003, 0x00000002, 0x00000006, 0x00000003,
        0xffffffff, 0xffffffff, 0x00000003, 0x00000003, 0x00000006, 0x00000003, 0xffffffff, 0xffffffff,
        0x00000003, 0x00000004, 0x00000006, 0x00000003, 0xffffffff, 0xffffffff, 0x00000003, 0x00000005,
        0x00000000, 0x00000002, 0x00000014, 0x00000007, 0x000002c1, 0x00000000, 0x00000002, 0x00000030,
        0x00000007, 0x000002c8, 0x00000000, 0x00000004, 0x0000001e, 0x00000002, 0x00000102, 0x00000000,
        0x00000004, 0x00000027, 0x00000007, 0x0000010b, 0x00000000, 0x00000006, 0x00000009, 0x00000003,
        0x00000131, 0x00000000, 0x00000001, 0x00000014, 0x00000006, 0x000002cf, 0x00000000, 0x00000004,
        0x00000005, 0x00000004, 0x000000e9, 0x00000000, 0x00000009, 0x00000004, 0x00000000, 0x00000000,
        0x00000003, 0x00000051, 0x00000003, 0x00000001, 0x00000000, 0x00000003, 0x00000076, 0x00000004,
        0x00000002, 0x00000004, 0x00000000, 0x000002b4, 0x00000007, 0x00000001, 0x0000000c, 0x00000003,
        0x00000076, 0x00000004, 0x00000002, 0x00000004, 0x00000001, 0x000002bb, 0x00000006, 0x00000001,
        0x00000010, 0x00000004, 0x000000e9, 0x00000004, 0x00000003, 0x00000014, 0x00000005, 0x00000000,
        0x00000001, 0x00000001, 0x00000003, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000003,
        0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000001, 0xffffffff, 0x00000001,
        0x00000014, 0x00000004, 0x00000004, 0xffffffff, 0x00000001, 0x00000000, 0x00000000, 0x00000001,
        0x00000001, 0xffffffff, 0x00000001, 0x00000008, 0x00000004, 0x00000002, 0xffffffff, 0x00000006,
        0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x00000000, 0x00000000,
        0x00000006, 0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000001, 0x00000020, 0x0000000c, 0x00000018, 0xffffffff, 0x00000003, 0x00000000, 0x00000000,
        0x00000001, 0x00000001, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0xffffffff,
        0x00000004, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000000,
        0x00000000, 0x00000006, 0xffffffff, 0x00000000, 0x00000001, 0x00000002, 0x00000003, 0x00000006,
        0x00000004, 0x00000005, 0x00000006, 0x00000008, 0x00000004, 0x00000005, 0x00000002, 0x505c3a43,
        0x72676f72, 0x656d6d61, 0x63694d5c, 0x6f736f72, 0x44207466, 0x63657269, 0x53205874, 0x28204b44,
        0x656e754a, 0x31303220, 0x555c2930, 0x696c6974, 0x73656974, 0x6e69625c, 0x3638785c, 0x6165685c,
        0x2e726564, 0x74737866, 0x74637572, 0x4f535620, 0x66207b20, 0x74616f6c, 0x20702033, 0x4f50203a,
        0x49544953, 0x203b4e4f, 0x730a3b7d, 0x63757274, 0x53482074, 0x7b204f44, 0x6f6c6620, 0x65207461,
        0x205d345b, 0x5653203a, 0x7365545f, 0x63614673, 0x3b726f74, 0x6f6c6620, 0x69207461, 0x205d325b,
        0x5653203a, 0x736e495f, 0x54656469, 0x46737365, 0x6f746361, 0x7d203b72, 0x74730a3b, 0x74637572,
        0x4f534820, 0x66207b20, 0x74616f6c, 0x20702033, 0x4542203a, 0x5245495a, 0x3b534f50, 0x0a3b7d20,
        0x4f445348, 0x48434220, 0x49202853, 0x7475706e, 0x63746150, 0x53563c68, 0x38202c4f, 0x7069203e,
        0x6975202c, 0x5020746e, 0x68637461, 0x3a204449, 0x5f565320, 0x6d697250, 0x76697469, 0x20444965,
        0x0a7b0a29, 0x20202020, 0x4f445348, 0x73657220, 0x20200a3b, 0x65722020, 0x5b652e73, 0x3d205d30,
        0x73657220, 0x315b652e, 0x203d205d, 0x2e736572, 0x5d325b65, 0x72203d20, 0x652e7365, 0x205d335b,
        0x2e31203d, 0x0a3b6630, 0x20202020, 0x2e736572, 0x5d305b69, 0x72203d20, 0x692e7365, 0x205d315b,
        0x2e31203d, 0x0a3b6630, 0x20202020, 0x75746572, 0x72206e72, 0x0a3b7365, 0x645b0a7d, 0x69616d6f,
        0x7122286e, 0x22646175, 0x5b0a5d29, 0x74726170, 0x6f697469, 0x676e696e, 0x6e692228, 0x65676574,
        0x5d292272, 0x756f5b0a, 0x74757074, 0x6f706f74, 0x79676f6c, 0x72742228, 0x676e6169, 0x635f656c,
        0x5d292277, 0x756f5b0a, 0x74757074, 0x746e6f63, 0x706c6f72, 0x746e696f, 0x29382873, 0x705b0a5d,
        0x68637461, 0x736e6f63, 0x746e6174, 0x636e7566, 0x43422228, 0x29225348, 0x53480a5d, 0x4842204f,
        0x49202853, 0x7475706e, 0x63746150, 0x53563c68, 0x38202c4f, 0x2c70203e, 0x6e697520, 0x20692074,
        0x5653203a, 0x74754f5f, 0x43747570, 0x72746e6f, 0x6f506c6f, 0x49746e69, 0x75202c44, 0x20746e69,
        0x63746150, 0x20444968, 0x5653203a, 0x6972505f, 0x6974696d, 0x44496576, 0x7b0a2920, 0x2020200a,
        0x4f534820, 0x73657220, 0x20200a3b, 0x65722020, 0x20702e73, 0x5b70203d, 0x702e5d69, 0x20200a3b,
        0x65722020, 0x6e727574, 0x73657220, 0x0a7d0a3b, 0x626f6c47, 0x4c736c61, 0x6c61636f, 0x44534873,
        0x653a3a4f, 0x4f445348, 0x56693a3a, 0x3a3a4f53, 0x63694d70, 0x6f736f72, 0x28207466, 0x48202952,
        0x204c534c, 0x64616853, 0x43207265, 0x69706d6f, 0x2072656c, 0x39322e39, 0x3235392e, 0x3131332e,
        0x48420031, 0x73680053, 0x305f355f, 0x6e6f6320, 0x6c6f7274, 0x696f7020, 0x0000746e,
    };

    static const D3D_BLOB_PART parts[] =
    {
       D3D_BLOB_INPUT_SIGNATURE_BLOB,
       D3D_BLOB_OUTPUT_SIGNATURE_BLOB,
       D3D_BLOB_INPUT_AND_OUTPUT_SIGNATURE_BLOB,
       D3D_BLOB_PATCH_CONSTANT_SIGNATURE_BLOB,
       D3D_BLOB_ALL_SIGNATURE_BLOB,
       D3D_BLOB_DEBUG_INFO,
       D3D_BLOB_LEGACY_SHADER,
       D3D_BLOB_XNA_PREPASS_SHADER,
       D3D_BLOB_XNA_SHADER,
       D3D_BLOB_TEST_ALTERNATE_SHADER,
       D3D_BLOB_TEST_COMPILE_DETAILS,
       D3D_BLOB_TEST_COMPILE_PERF,
    };

    unsigned int refcount, size, i;
    ID3DBlob *blob, *blob2;
    uint32_t *u32;
    HRESULT hr;

    hr = D3DCreateBlob(1, &blob2);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    blob = blob2;

    /* Invalid cases. */
    hr = D3DGetBlobPart(NULL, test_blob_part[6], D3D_BLOB_INPUT_SIGNATURE_BLOB, 0, &blob);
    ok(hr == D3DERR_INVALIDCALL, "Got hr %#x.\n", hr);
    ok(blob == blob2, "Got blob %p, expected %p.\n", blob, blob2);

    hr = D3DGetBlobPart(NULL, 0, D3D_BLOB_INPUT_SIGNATURE_BLOB, 0, &blob);
    ok(hr == D3DERR_INVALIDCALL, "Got hr %#x.\n", hr);
    ok(blob == blob2, "Got blob %p, expected %p.\n", blob, blob2);

    hr = D3DGetBlobPart(NULL, test_blob_part[6], D3D_BLOB_INPUT_SIGNATURE_BLOB, 0, NULL);
    ok(hr == D3DERR_INVALIDCALL, "Got hr %#x.\n", hr);

    hr = D3DGetBlobPart(NULL, 0, D3D_BLOB_INPUT_SIGNATURE_BLOB, 0, NULL);
    ok(hr == D3DERR_INVALIDCALL, "Got hr %#x.\n", hr);

    hr = D3DGetBlobPart(test_blob_part, 0, D3D_BLOB_INPUT_SIGNATURE_BLOB, 0, &blob);
    ok(hr == D3DERR_INVALIDCALL, "Got hr %#x.\n", hr);
    ok(blob == blob2, "Got blob %p, expected %p.\n", blob, blob2);

    hr = D3DGetBlobPart(test_blob_part, 7 * sizeof(DWORD), D3D_BLOB_INPUT_SIGNATURE_BLOB, 0, &blob);
    ok(hr == D3DERR_INVALIDCALL, "Got hr %#x.\n", hr);
    ok(blob == blob2, "Got blob %p, expected %p.\n", blob, blob2);

    hr = D3DGetBlobPart(test_blob_part, 8 * sizeof(DWORD), D3D_BLOB_INPUT_SIGNATURE_BLOB, 0, &blob);
    ok(hr == D3DERR_INVALIDCALL, "Got hr %#x.\n", hr);
    ok(blob == blob2, "Got blob %p, expected %p.\n", blob, blob2);

    hr = D3DGetBlobPart(test_blob_part, test_blob_part[6], D3D_BLOB_INPUT_SIGNATURE_BLOB, 0, NULL);
    ok(hr == D3DERR_INVALIDCALL, "Got hr %#x.\n", hr);

    hr = D3DGetBlobPart(test_blob_part, 0, D3D_BLOB_INPUT_SIGNATURE_BLOB, 0, NULL);
    ok(hr == D3DERR_INVALIDCALL, "Got hr %#x.\n", hr);

    hr = D3DGetBlobPart(test_blob_part, test_blob_part[6], D3D_BLOB_INPUT_SIGNATURE_BLOB, 1, &blob);
    ok(hr == D3DERR_INVALIDCALL, "Got hr %#x.\n", hr);
    ok(blob2 == blob, "D3DGetBlobPart failed got %p, expected %p\n", blob, blob2);

    hr = D3DGetBlobPart(test_blob_part, test_blob_part[6], 0xffffffff, 0, &blob);
    ok(hr == D3DERR_INVALIDCALL, "Got hr %#x.\n", hr);
    ok(blob == blob2, "Got blob %p, expected %p.\n", blob, blob2);

    refcount = ID3D10Blob_Release(blob2);
    ok(!refcount, "Got refcount %u.\n", refcount);

    /* D3D_BLOB_INPUT_SIGNATURE_BLOB */
    hr = D3DGetBlobPart(test_blob_part, test_blob_part[6], D3D_BLOB_INPUT_SIGNATURE_BLOB, 0, &blob);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    size = ID3D10Blob_GetBufferSize(blob);
    ok(size == 124, "Got size %u.\n", size);

    u32 = ID3D10Blob_GetBufferPointer(blob);
    ok(u32[0] == TAG_DXBC, "Got u32[0] 0x%08x, expected 0x%08x.\n", u32[0], TAG_DXBC);
    ok(u32[9] == TAG_ISGN, "Got u32[9] 0x%08x, expected 0x%08x.\n", u32[9], TAG_ISGN);

    for (i = 0; i < ARRAY_SIZE(parts); ++i)
    {
        vkd3d_test_push_context("Part %#x", parts[i]);
        hr = D3DGetBlobPart(u32, size, parts[i], 0, &blob2);

        if (parts[i] == D3D_BLOB_INPUT_SIGNATURE_BLOB)
        {
            ok(hr == S_OK, "Got hr %#x.\n", hr);

            refcount = ID3D10Blob_Release(blob2);
            ok(!refcount, "Got refcount %u.\n", refcount);
        }
        else
        {
            ok(hr == E_FAIL, "Got hr %#x.\n", hr);
        }
        vkd3d_test_pop_context();
    }

    refcount = ID3D10Blob_Release(blob);
    ok(!refcount, "Got refcount %u.\n", refcount);

    /* D3D_BLOB_OUTPUT_SIGNATURE_BLOB */
    hr = D3DGetBlobPart(test_blob_part, test_blob_part[6], D3D_BLOB_OUTPUT_SIGNATURE_BLOB, 0, &blob);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    size = ID3D10Blob_GetBufferSize(blob);
    ok(size == 88, "Got size %u.\n", size);

    u32 = ID3D10Blob_GetBufferPointer(blob);
    ok(u32[0] == TAG_DXBC, "Got u32[0] 0x%08x, expected 0x%08x.\n", u32[0], TAG_DXBC);
    ok(u32[9] == TAG_OSGN, "Got u32[9] 0x%08x, expected 0x%08x.\n", u32[9], TAG_OSGN);

    for (i = 0; i < ARRAY_SIZE(parts); ++i)
    {
        vkd3d_test_push_context("Part %#x", parts[i]);
        hr = D3DGetBlobPart(u32, size, parts[i], 0, &blob2);

        if (parts[i] == D3D_BLOB_OUTPUT_SIGNATURE_BLOB)
        {
            ok(hr == S_OK, "Got hr %#x.\n", hr);

            refcount = ID3D10Blob_Release(blob2);
            ok(!refcount, "Got refcount %u.\n", refcount);
        }
        else
        {
            ok(hr == E_FAIL, "Got hr %#x.\n", hr);
        }
        vkd3d_test_pop_context();
    }

    refcount = ID3D10Blob_Release(blob);
    ok(!refcount, "Got refcount %u.\n", refcount);

    /* D3D_BLOB_INPUT_AND_OUTPUT_SIGNATURE_BLOB */
    hr = D3DGetBlobPart(test_blob_part, test_blob_part[6], D3D_BLOB_INPUT_AND_OUTPUT_SIGNATURE_BLOB, 0, &blob);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    size = ID3D10Blob_GetBufferSize(blob);
    ok(size == 180, "Got size %u.\n", size);

    u32 = ID3D10Blob_GetBufferPointer(blob);
    ok(u32[0] == TAG_DXBC, "Got u32[0] 0x%08x, expected 0x%08x.\n", u32[0], TAG_DXBC);
    ok(u32[10] == TAG_ISGN, "Got u32[10] 0x%08x, expected 0x%08x.\n", u32[10], TAG_ISGN);
    ok(u32[32] == TAG_OSGN, "Got u32[32] 0x%08x, expected 0x%08x.\n", u32[10], TAG_OSGN);

    for (i = 0; i < ARRAY_SIZE(parts); ++i)
    {
        vkd3d_test_push_context("Part %#x", parts[i]);
        hr = D3DGetBlobPart(u32, size, parts[i], 0, &blob2);

        if (parts[i] == D3D_BLOB_INPUT_AND_OUTPUT_SIGNATURE_BLOB
                || parts[i] == D3D_BLOB_INPUT_SIGNATURE_BLOB
                || parts[i] == D3D_BLOB_OUTPUT_SIGNATURE_BLOB)
        {
            ok(hr == S_OK, "Got hr %#x.\n", hr);

            refcount = ID3D10Blob_Release(blob2);
            ok(!refcount, "Got refcount %u.\n", refcount);
        }
        else
        {
            ok(hr == E_FAIL, "Got hr %#x.\n", hr);
        }
        vkd3d_test_pop_context();
    }

    refcount = ID3D10Blob_Release(blob);
    ok(!refcount, "Got refcount %u.\n", refcount);

    /* D3D_BLOB_PATCH_CONSTANT_SIGNATURE_BLOB */
    hr = D3DGetBlobPart(test_blob_part, test_blob_part[6], D3D_BLOB_PATCH_CONSTANT_SIGNATURE_BLOB, 0, &blob);
    ok(hr == E_FAIL, "Got hr %#x.\n", hr);

    /* D3D_BLOB_ALL_SIGNATURE_BLOB */
    hr = D3DGetBlobPart(test_blob_part, test_blob_part[6], D3D_BLOB_ALL_SIGNATURE_BLOB, 0, &blob);
    ok(hr == E_FAIL, "Got hr %#x.\n", hr);

    /* D3D_BLOB_DEBUG_INFO */
    hr = D3DGetBlobPart(test_blob_part, test_blob_part[6], D3D_BLOB_DEBUG_INFO, 0, &blob);
    ok(hr == E_FAIL, "Got hr %#x.\n", hr);

    /* D3D_BLOB_LEGACY_SHADER */
    hr = D3DGetBlobPart(test_blob_part, test_blob_part[6], D3D_BLOB_LEGACY_SHADER, 0, &blob);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    size = ID3D10Blob_GetBufferSize(blob);
    ok(size == 92, "Got size %u.\n", size);

    u32 = ID3D10Blob_GetBufferPointer(blob);
    ok(u32[0] != TAG_DXBC, "Got u32[0] 0x%08x.\n", u32[0]);

    for (i = 0; i < ARRAY_SIZE(parts); ++i)
    {
        vkd3d_test_push_context("Part %#x", parts[i]);
        /* D3D_BLOB_LEGACY_SHADER doesn't return a full DXBC blob. */
        hr = D3DGetBlobPart(u32, size, parts[i], 0, &blob2);
        ok(hr == D3DERR_INVALIDCALL, "Got hr %#x.\n", hr);
        vkd3d_test_pop_context();
    }

    refcount = ID3D10Blob_Release(blob);
    ok(!refcount, "Got refcount %u.\n", refcount);

    /* D3D_BLOB_XNA_PREPASS_SHADER */
    hr = D3DGetBlobPart(test_blob_part, test_blob_part[6], D3D_BLOB_XNA_PREPASS_SHADER, 0, &blob);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    size = ID3D10Blob_GetBufferSize(blob);
    ok(size == 68, "Got size %u.\n", size);

    u32 = ID3D10Blob_GetBufferPointer(blob);
    ok(u32[0] != TAG_DXBC, "Got u32[0] 0x%08x.\n", u32[0]);

    for (i = 0; i < ARRAY_SIZE(parts); ++i)
    {
        vkd3d_test_push_context("Part %#x", parts[i]);
        /* D3D_BLOB_XNA_PREPASS_SHADER doesn't return a full DXBC blob. */
        hr = D3DGetBlobPart(u32, size, parts[i], 0, &blob2);
        ok(hr == D3DERR_INVALIDCALL, "Got hr %#x.\n", hr);
        vkd3d_test_pop_context();
    }

    refcount = ID3D10Blob_Release(blob);
    ok(!refcount, "Got refcount %u.\n", refcount);

    /* D3D_BLOB_XNA_SHADER */
    hr = D3DGetBlobPart(test_blob_part, test_blob_part[6], D3D_BLOB_XNA_SHADER, 0, &blob);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    size = ID3D10Blob_GetBufferSize(blob);
    ok(size == 68, "Got size %u.\n", size);

    u32 = ID3D10Blob_GetBufferPointer(blob);
    ok(u32[0] != TAG_DXBC, "Got u32[0] 0x%08x.\n", u32[0]);

    for (i = 0; i < ARRAY_SIZE(parts); ++i)
    {
        vkd3d_test_push_context("Part %#x", parts[i]);
        /* D3D_BLOB_XNA_SHADER doesn't return a full DXBC blob. */
        hr = D3DGetBlobPart(u32, size, parts[i], 0, &blob2);
        ok(hr == D3DERR_INVALIDCALL, "Got hr %#x.\n", hr);
        vkd3d_test_pop_context();
    }

    refcount = ID3D10Blob_Release(blob);
    ok(!refcount, "Got refcount %u.\n", refcount);

    /* D3D_BLOB_PATCH_CONSTANT_SIGNATURE_BLOB */
    hr = D3DGetBlobPart(test_blob_part2, test_blob_part2[6], D3D_BLOB_PATCH_CONSTANT_SIGNATURE_BLOB, 0, &blob);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    size = ID3D10Blob_GetBufferSize(blob);
    ok(size == 232, "Got size %u.\n", size);

    u32 = ID3D10Blob_GetBufferPointer(blob);
    ok(u32[0] == TAG_DXBC, "Got u32[0] 0x%08x, expected 0x%08x.\n", u32[0], TAG_DXBC);
    ok(u32[9] == TAG_PCSG, "Got u32[9] 0x%08x, expected 0x%08x.\n", u32[9], TAG_PCSG);

    for (i = 0; i < ARRAY_SIZE(parts); ++i)
    {
        vkd3d_test_push_context("Part %#x", parts[i]);
        hr = D3DGetBlobPart(u32, size, parts[i], 0, &blob2);

        if (parts[i] == D3D_BLOB_PATCH_CONSTANT_SIGNATURE_BLOB)
        {
            ok(hr == S_OK, "Got hr %#x.\n", hr);

            refcount = ID3D10Blob_Release(blob2);
            ok(!refcount, "Got refcount %u.\n", refcount);
        }
        else
        {
            ok(hr == E_FAIL, "Got hr %#x.\n", hr);
        }
        vkd3d_test_pop_context();
    }

    refcount = ID3D10Blob_Release(blob);
    ok(!refcount, "Got refcount %u.\n", refcount);

    /* D3D_BLOB_ALL_SIGNATURE_BLOB */
    hr = D3DGetBlobPart(test_blob_part2, test_blob_part2[6], D3D_BLOB_ALL_SIGNATURE_BLOB, 0, &blob);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    size = ID3D10Blob_GetBufferSize(blob);
    ok(size == 344, "Got size %u.\n", size);

    u32 = ID3D10Blob_GetBufferPointer(blob);
    ok(u32[0] == TAG_DXBC, "Got u32[0] 0x%08x, expected 0x%08x.\n", u32[0], TAG_DXBC);
    ok(u32[11] == TAG_ISGN, "Got u32[11] 0x%08x, expected 0x%08x.\n", u32[11], TAG_ISGN);
    ok(u32[24] == TAG_OSGN, "Got u32[24] 0x%08x, expected 0x%08x.\n", u32[24], TAG_OSGN);
    ok(u32[37] == TAG_PCSG, "Got u32[37] 0x%08x, expected 0x%08x.\n", u32[37], TAG_PCSG);

    for (i = 0; i < ARRAY_SIZE(parts); ++i)
    {
        vkd3d_test_push_context("Part %#x", parts[i]);
        hr = D3DGetBlobPart(u32, size, parts[i], 0, &blob2);

        if (parts[i] == D3D_BLOB_ALL_SIGNATURE_BLOB
                || parts[i] == D3D_BLOB_PATCH_CONSTANT_SIGNATURE_BLOB
                || parts[i] == D3D_BLOB_INPUT_AND_OUTPUT_SIGNATURE_BLOB
                || parts[i] == D3D_BLOB_INPUT_SIGNATURE_BLOB
                || parts[i] == D3D_BLOB_OUTPUT_SIGNATURE_BLOB)
        {
            ok(hr == S_OK, "Got hr %#x.\n", hr);

            refcount = ID3D10Blob_Release(blob2);
            ok(!refcount, "Got refcount %u.\n", refcount);
        }
        else
        {
            ok(hr == E_FAIL, "Got hr %#x.\n", hr);
        }
        vkd3d_test_pop_context();
    }

    refcount = ID3D10Blob_Release(blob);
    ok(!refcount, "Got refcount %u.\n", refcount);

    /* D3D_BLOB_DEBUG_INFO */
    hr = D3DGetBlobPart(test_blob_part2, test_blob_part2[6], D3D_BLOB_DEBUG_INFO, 0, &blob);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    size = ID3D10Blob_GetBufferSize(blob);
    ok(size == 4055, "Got size %u.\n", size);

    u32 = ID3D10Blob_GetBufferPointer(blob);
    ok(u32[0] != TAG_DXBC, "Got u32[0] 0x%08x.\n", u32[0]);

    for (i = 0; i < ARRAY_SIZE(parts); ++i)
    {
        vkd3d_test_push_context("Part %#x", parts[i]);
        /* D3D_BLOB_DEBUG_INFO doesn't return a full DXBC blob. */
        hr = D3DGetBlobPart(u32, size, parts[i], 0, &blob2);
        ok(hr == D3DERR_INVALIDCALL, "Got hr %#x.\n", hr);
        vkd3d_test_pop_context();
    }

    refcount = ID3D10Blob_Release(blob);
    ok(!refcount, "Got refcount %u.\n", refcount);

    /* D3D_BLOB_LEGACY_SHADER */
    hr = D3DGetBlobPart(test_blob_part2, test_blob_part2[6], D3D_BLOB_LEGACY_SHADER, 0, &blob);
    ok(hr == E_FAIL, "Got hr %#x.\n", hr);

    /* D3D_BLOB_XNA_PREPASS_SHADER */
    hr = D3DGetBlobPart(test_blob_part2, test_blob_part2[6], D3D_BLOB_XNA_PREPASS_SHADER, 0, &blob);
    ok(hr == E_FAIL, "Got hr %#x.\n", hr);

    /* D3D_BLOB_XNA_SHADER */
    hr = D3DGetBlobPart(test_blob_part2, test_blob_part2[6], D3D_BLOB_XNA_SHADER, 0, &blob);
    ok(hr == E_FAIL, "Got hr %#x.\n", hr);
}

START_TEST(hlsl_d3d12)
{
    parse_args(argc, argv);
    enable_d3d12_debug_layer();
    init_adapter_info();

    run_test(test_preprocess);
    run_test(test_thread_id);
    run_test(test_create_blob);
    run_test(test_get_blob_part);
}
