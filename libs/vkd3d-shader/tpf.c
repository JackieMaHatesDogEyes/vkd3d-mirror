/*
 * TPF (Direct3D shader models 4 and 5 bytecode) support
 *
 * Copyright 2008-2009 Henri Verbeet for CodeWeavers
 * Copyright 2010 Rico Schüller
 * Copyright 2017 Józef Kucia for CodeWeavers
 * Copyright 2019-2020 Zebediah Figura for CodeWeavers
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

#include "hlsl.h"

#define SM4_MAX_SRC_COUNT 6
#define SM4_MAX_DST_COUNT 2

STATIC_ASSERT(SM4_MAX_SRC_COUNT <= SPIRV_MAX_SRC_COUNT);

#define VKD3D_SM4_PS  0x0000u
#define VKD3D_SM4_VS  0x0001u
#define VKD3D_SM4_GS  0x0002u
#define VKD3D_SM5_HS  0x0003u
#define VKD3D_SM5_DS  0x0004u
#define VKD3D_SM5_CS  0x0005u
#define VKD3D_SM4_LIB 0xfff0u

#define VKD3D_SM4_INSTRUCTION_MODIFIER        (0x1u << 31)

#define VKD3D_SM4_MODIFIER_MASK               0x3fu

#define VKD3D_SM5_MODIFIER_DATA_TYPE_SHIFT    6
#define VKD3D_SM5_MODIFIER_DATA_TYPE_MASK     (0xffffu << VKD3D_SM5_MODIFIER_DATA_TYPE_SHIFT)

#define VKD3D_SM5_MODIFIER_RESOURCE_TYPE_SHIFT 6
#define VKD3D_SM5_MODIFIER_RESOURCE_TYPE_MASK (0xfu << VKD3D_SM5_MODIFIER_RESOURCE_TYPE_SHIFT)

#define VKD3D_SM5_MODIFIER_RESOURCE_STRIDE_SHIFT 11
#define VKD3D_SM5_MODIFIER_RESOURCE_STRIDE_MASK  (0xfffu << VKD3D_SM5_MODIFIER_RESOURCE_STRIDE_SHIFT)

#define VKD3D_SM4_AOFFIMMI_U_SHIFT            9
#define VKD3D_SM4_AOFFIMMI_U_MASK             (0xfu << VKD3D_SM4_AOFFIMMI_U_SHIFT)
#define VKD3D_SM4_AOFFIMMI_V_SHIFT            13
#define VKD3D_SM4_AOFFIMMI_V_MASK             (0xfu << VKD3D_SM4_AOFFIMMI_V_SHIFT)
#define VKD3D_SM4_AOFFIMMI_W_SHIFT            17
#define VKD3D_SM4_AOFFIMMI_W_MASK             (0xfu << VKD3D_SM4_AOFFIMMI_W_SHIFT)

#define VKD3D_SM4_INSTRUCTION_LENGTH_SHIFT    24
#define VKD3D_SM4_INSTRUCTION_LENGTH_MASK     (0x1fu << VKD3D_SM4_INSTRUCTION_LENGTH_SHIFT)

#define VKD3D_SM4_INSTRUCTION_FLAGS_SHIFT     11
#define VKD3D_SM4_INSTRUCTION_FLAGS_MASK      (0x7u << VKD3D_SM4_INSTRUCTION_FLAGS_SHIFT)

#define VKD3D_SM4_RESOURCE_TYPE_SHIFT         11
#define VKD3D_SM4_RESOURCE_TYPE_MASK          (0xfu << VKD3D_SM4_RESOURCE_TYPE_SHIFT)

#define VKD3D_SM4_RESOURCE_SAMPLE_COUNT_SHIFT 16
#define VKD3D_SM4_RESOURCE_SAMPLE_COUNT_MASK  (0xfu << VKD3D_SM4_RESOURCE_SAMPLE_COUNT_SHIFT)

#define VKD3D_SM4_PRIMITIVE_TYPE_SHIFT        11
#define VKD3D_SM4_PRIMITIVE_TYPE_MASK         (0x3fu << VKD3D_SM4_PRIMITIVE_TYPE_SHIFT)

#define VKD3D_SM4_INDEX_TYPE_SHIFT            11
#define VKD3D_SM4_INDEX_TYPE_MASK             (0x1u << VKD3D_SM4_INDEX_TYPE_SHIFT)

#define VKD3D_SM4_SAMPLER_MODE_SHIFT          11
#define VKD3D_SM4_SAMPLER_MODE_MASK           (0xfu << VKD3D_SM4_SAMPLER_MODE_SHIFT)

#define VKD3D_SM4_SHADER_DATA_TYPE_SHIFT      11
#define VKD3D_SM4_SHADER_DATA_TYPE_MASK       (0xfu << VKD3D_SM4_SHADER_DATA_TYPE_SHIFT)

#define VKD3D_SM4_INTERPOLATION_MODE_SHIFT    11
#define VKD3D_SM4_INTERPOLATION_MODE_MASK     (0xfu << VKD3D_SM4_INTERPOLATION_MODE_SHIFT)

#define VKD3D_SM4_GLOBAL_FLAGS_SHIFT          11
#define VKD3D_SM4_GLOBAL_FLAGS_MASK           (0xffu << VKD3D_SM4_GLOBAL_FLAGS_SHIFT)

#define VKD3D_SM5_PRECISE_SHIFT               19
#define VKD3D_SM5_PRECISE_MASK                (0xfu << VKD3D_SM5_PRECISE_SHIFT)

#define VKD3D_SM5_CONTROL_POINT_COUNT_SHIFT   11
#define VKD3D_SM5_CONTROL_POINT_COUNT_MASK    (0xffu << VKD3D_SM5_CONTROL_POINT_COUNT_SHIFT)

#define VKD3D_SM5_FP_ARRAY_SIZE_SHIFT         16
#define VKD3D_SM5_FP_TABLE_COUNT_MASK         0xffffu

#define VKD3D_SM5_UAV_FLAGS_SHIFT             15
#define VKD3D_SM5_UAV_FLAGS_MASK              (0x1ffu << VKD3D_SM5_UAV_FLAGS_SHIFT)

#define VKD3D_SM5_SYNC_FLAGS_SHIFT            11
#define VKD3D_SM5_SYNC_FLAGS_MASK             (0xffu << VKD3D_SM5_SYNC_FLAGS_SHIFT)

#define VKD3D_SM5_TESSELLATOR_SHIFT           11
#define VKD3D_SM5_TESSELLATOR_MASK            (0xfu << VKD3D_SM5_TESSELLATOR_SHIFT)

#define VKD3D_SM4_OPCODE_MASK                 0xff

#define VKD3D_SM4_EXTENDED_OPERAND            (0x1u << 31)

#define VKD3D_SM4_EXTENDED_OPERAND_TYPE_MASK  0x3fu

#define VKD3D_SM4_REGISTER_MODIFIER_SHIFT     6
#define VKD3D_SM4_REGISTER_MODIFIER_MASK      (0xffu << VKD3D_SM4_REGISTER_MODIFIER_SHIFT)

#define VKD3D_SM4_REGISTER_PRECISION_SHIFT    14
#define VKD3D_SM4_REGISTER_PRECISION_MASK     (0x7u << VKD3D_SM4_REGISTER_PRECISION_SHIFT)

#define VKD3D_SM4_REGISTER_NON_UNIFORM_SHIFT  17
#define VKD3D_SM4_REGISTER_NON_UNIFORM_MASK   (0x1u << VKD3D_SM4_REGISTER_NON_UNIFORM_SHIFT)

#define VKD3D_SM4_ADDRESSING_SHIFT2           28
#define VKD3D_SM4_ADDRESSING_MASK2            (0x3u << VKD3D_SM4_ADDRESSING_SHIFT2)

#define VKD3D_SM4_ADDRESSING_SHIFT1           25
#define VKD3D_SM4_ADDRESSING_MASK1            (0x3u << VKD3D_SM4_ADDRESSING_SHIFT1)

#define VKD3D_SM4_ADDRESSING_SHIFT0           22
#define VKD3D_SM4_ADDRESSING_MASK0            (0x3u << VKD3D_SM4_ADDRESSING_SHIFT0)

#define VKD3D_SM4_REGISTER_ORDER_SHIFT        20
#define VKD3D_SM4_REGISTER_ORDER_MASK         (0x3u << VKD3D_SM4_REGISTER_ORDER_SHIFT)

#define VKD3D_SM4_REGISTER_TYPE_SHIFT         12
#define VKD3D_SM4_REGISTER_TYPE_MASK          (0xffu << VKD3D_SM4_REGISTER_TYPE_SHIFT)

#define VKD3D_SM4_SWIZZLE_TYPE_SHIFT          2
#define VKD3D_SM4_SWIZZLE_TYPE_MASK           (0x3u << VKD3D_SM4_SWIZZLE_TYPE_SHIFT)

#define VKD3D_SM4_DIMENSION_SHIFT             0
#define VKD3D_SM4_DIMENSION_MASK              (0x3u << VKD3D_SM4_DIMENSION_SHIFT)

#define VKD3D_SM4_WRITEMASK_SHIFT             4
#define VKD3D_SM4_WRITEMASK_MASK              (0xfu << VKD3D_SM4_WRITEMASK_SHIFT)

#define VKD3D_SM4_SWIZZLE_SHIFT               4
#define VKD3D_SM4_SWIZZLE_MASK                (0xffu << VKD3D_SM4_SWIZZLE_SHIFT)

#define VKD3D_SM4_VERSION_MAJOR(version)      (((version) >> 4) & 0xf)
#define VKD3D_SM4_VERSION_MINOR(version)      (((version) >> 0) & 0xf)

#define VKD3D_SM4_ADDRESSING_RELATIVE         0x2
#define VKD3D_SM4_ADDRESSING_OFFSET           0x1

#define VKD3D_SM4_INSTRUCTION_FLAG_SATURATE   0x4

#define VKD3D_SM4_CONDITIONAL_NZ              (0x1u << 18)

#define VKD3D_SM4_TYPE_COMPONENT(com, i)      (((com) >> (4 * (i))) & 0xfu)

/* The shift that corresponds to the D3D_SIF_TEXTURE_COMPONENTS mask. */
#define VKD3D_SM4_SIF_TEXTURE_COMPONENTS_SHIFT 2

enum vkd3d_sm4_opcode
{
    VKD3D_SM4_OP_ADD                              = 0x00,
    VKD3D_SM4_OP_AND                              = 0x01,
    VKD3D_SM4_OP_BREAK                            = 0x02,
    VKD3D_SM4_OP_BREAKC                           = 0x03,
    VKD3D_SM4_OP_CASE                             = 0x06,
    VKD3D_SM4_OP_CONTINUE                         = 0x07,
    VKD3D_SM4_OP_CONTINUEC                        = 0x08,
    VKD3D_SM4_OP_CUT                              = 0x09,
    VKD3D_SM4_OP_DEFAULT                          = 0x0a,
    VKD3D_SM4_OP_DERIV_RTX                        = 0x0b,
    VKD3D_SM4_OP_DERIV_RTY                        = 0x0c,
    VKD3D_SM4_OP_DISCARD                          = 0x0d,
    VKD3D_SM4_OP_DIV                              = 0x0e,
    VKD3D_SM4_OP_DP2                              = 0x0f,
    VKD3D_SM4_OP_DP3                              = 0x10,
    VKD3D_SM4_OP_DP4                              = 0x11,
    VKD3D_SM4_OP_ELSE                             = 0x12,
    VKD3D_SM4_OP_EMIT                             = 0x13,
    VKD3D_SM4_OP_ENDIF                            = 0x15,
    VKD3D_SM4_OP_ENDLOOP                          = 0x16,
    VKD3D_SM4_OP_ENDSWITCH                        = 0x17,
    VKD3D_SM4_OP_EQ                               = 0x18,
    VKD3D_SM4_OP_EXP                              = 0x19,
    VKD3D_SM4_OP_FRC                              = 0x1a,
    VKD3D_SM4_OP_FTOI                             = 0x1b,
    VKD3D_SM4_OP_FTOU                             = 0x1c,
    VKD3D_SM4_OP_GE                               = 0x1d,
    VKD3D_SM4_OP_IADD                             = 0x1e,
    VKD3D_SM4_OP_IF                               = 0x1f,
    VKD3D_SM4_OP_IEQ                              = 0x20,
    VKD3D_SM4_OP_IGE                              = 0x21,
    VKD3D_SM4_OP_ILT                              = 0x22,
    VKD3D_SM4_OP_IMAD                             = 0x23,
    VKD3D_SM4_OP_IMAX                             = 0x24,
    VKD3D_SM4_OP_IMIN                             = 0x25,
    VKD3D_SM4_OP_IMUL                             = 0x26,
    VKD3D_SM4_OP_INE                              = 0x27,
    VKD3D_SM4_OP_INEG                             = 0x28,
    VKD3D_SM4_OP_ISHL                             = 0x29,
    VKD3D_SM4_OP_ISHR                             = 0x2a,
    VKD3D_SM4_OP_ITOF                             = 0x2b,
    VKD3D_SM4_OP_LABEL                            = 0x2c,
    VKD3D_SM4_OP_LD                               = 0x2d,
    VKD3D_SM4_OP_LD2DMS                           = 0x2e,
    VKD3D_SM4_OP_LOG                              = 0x2f,
    VKD3D_SM4_OP_LOOP                             = 0x30,
    VKD3D_SM4_OP_LT                               = 0x31,
    VKD3D_SM4_OP_MAD                              = 0x32,
    VKD3D_SM4_OP_MIN                              = 0x33,
    VKD3D_SM4_OP_MAX                              = 0x34,
    VKD3D_SM4_OP_SHADER_DATA                      = 0x35,
    VKD3D_SM4_OP_MOV                              = 0x36,
    VKD3D_SM4_OP_MOVC                             = 0x37,
    VKD3D_SM4_OP_MUL                              = 0x38,
    VKD3D_SM4_OP_NE                               = 0x39,
    VKD3D_SM4_OP_NOP                              = 0x3a,
    VKD3D_SM4_OP_NOT                              = 0x3b,
    VKD3D_SM4_OP_OR                               = 0x3c,
    VKD3D_SM4_OP_RESINFO                          = 0x3d,
    VKD3D_SM4_OP_RET                              = 0x3e,
    VKD3D_SM4_OP_RETC                             = 0x3f,
    VKD3D_SM4_OP_ROUND_NE                         = 0x40,
    VKD3D_SM4_OP_ROUND_NI                         = 0x41,
    VKD3D_SM4_OP_ROUND_PI                         = 0x42,
    VKD3D_SM4_OP_ROUND_Z                          = 0x43,
    VKD3D_SM4_OP_RSQ                              = 0x44,
    VKD3D_SM4_OP_SAMPLE                           = 0x45,
    VKD3D_SM4_OP_SAMPLE_C                         = 0x46,
    VKD3D_SM4_OP_SAMPLE_C_LZ                      = 0x47,
    VKD3D_SM4_OP_SAMPLE_LOD                       = 0x48,
    VKD3D_SM4_OP_SAMPLE_GRAD                      = 0x49,
    VKD3D_SM4_OP_SAMPLE_B                         = 0x4a,
    VKD3D_SM4_OP_SQRT                             = 0x4b,
    VKD3D_SM4_OP_SWITCH                           = 0x4c,
    VKD3D_SM4_OP_SINCOS                           = 0x4d,
    VKD3D_SM4_OP_UDIV                             = 0x4e,
    VKD3D_SM4_OP_ULT                              = 0x4f,
    VKD3D_SM4_OP_UGE                              = 0x50,
    VKD3D_SM4_OP_UMUL                             = 0x51,
    VKD3D_SM4_OP_UMAX                             = 0x53,
    VKD3D_SM4_OP_UMIN                             = 0x54,
    VKD3D_SM4_OP_USHR                             = 0x55,
    VKD3D_SM4_OP_UTOF                             = 0x56,
    VKD3D_SM4_OP_XOR                              = 0x57,
    VKD3D_SM4_OP_DCL_RESOURCE                     = 0x58,
    VKD3D_SM4_OP_DCL_CONSTANT_BUFFER              = 0x59,
    VKD3D_SM4_OP_DCL_SAMPLER                      = 0x5a,
    VKD3D_SM4_OP_DCL_INDEX_RANGE                  = 0x5b,
    VKD3D_SM4_OP_DCL_OUTPUT_TOPOLOGY              = 0x5c,
    VKD3D_SM4_OP_DCL_INPUT_PRIMITIVE              = 0x5d,
    VKD3D_SM4_OP_DCL_VERTICES_OUT                 = 0x5e,
    VKD3D_SM4_OP_DCL_INPUT                        = 0x5f,
    VKD3D_SM4_OP_DCL_INPUT_SGV                    = 0x60,
    VKD3D_SM4_OP_DCL_INPUT_SIV                    = 0x61,
    VKD3D_SM4_OP_DCL_INPUT_PS                     = 0x62,
    VKD3D_SM4_OP_DCL_INPUT_PS_SGV                 = 0x63,
    VKD3D_SM4_OP_DCL_INPUT_PS_SIV                 = 0x64,
    VKD3D_SM4_OP_DCL_OUTPUT                       = 0x65,
    VKD3D_SM4_OP_DCL_OUTPUT_SIV                   = 0x67,
    VKD3D_SM4_OP_DCL_TEMPS                        = 0x68,
    VKD3D_SM4_OP_DCL_INDEXABLE_TEMP               = 0x69,
    VKD3D_SM4_OP_DCL_GLOBAL_FLAGS                 = 0x6a,
    VKD3D_SM4_OP_LOD                              = 0x6c,
    VKD3D_SM4_OP_GATHER4                          = 0x6d,
    VKD3D_SM4_OP_SAMPLE_POS                       = 0x6e,
    VKD3D_SM4_OP_SAMPLE_INFO                      = 0x6f,
    VKD3D_SM5_OP_HS_DECLS                         = 0x71,
    VKD3D_SM5_OP_HS_CONTROL_POINT_PHASE           = 0x72,
    VKD3D_SM5_OP_HS_FORK_PHASE                    = 0x73,
    VKD3D_SM5_OP_HS_JOIN_PHASE                    = 0x74,
    VKD3D_SM5_OP_EMIT_STREAM                      = 0x75,
    VKD3D_SM5_OP_CUT_STREAM                       = 0x76,
    VKD3D_SM5_OP_FCALL                            = 0x78,
    VKD3D_SM5_OP_BUFINFO                          = 0x79,
    VKD3D_SM5_OP_DERIV_RTX_COARSE                 = 0x7a,
    VKD3D_SM5_OP_DERIV_RTX_FINE                   = 0x7b,
    VKD3D_SM5_OP_DERIV_RTY_COARSE                 = 0x7c,
    VKD3D_SM5_OP_DERIV_RTY_FINE                   = 0x7d,
    VKD3D_SM5_OP_GATHER4_C                        = 0x7e,
    VKD3D_SM5_OP_GATHER4_PO                       = 0x7f,
    VKD3D_SM5_OP_GATHER4_PO_C                     = 0x80,
    VKD3D_SM5_OP_RCP                              = 0x81,
    VKD3D_SM5_OP_F32TOF16                         = 0x82,
    VKD3D_SM5_OP_F16TOF32                         = 0x83,
    VKD3D_SM5_OP_COUNTBITS                        = 0x86,
    VKD3D_SM5_OP_FIRSTBIT_HI                      = 0x87,
    VKD3D_SM5_OP_FIRSTBIT_LO                      = 0x88,
    VKD3D_SM5_OP_FIRSTBIT_SHI                     = 0x89,
    VKD3D_SM5_OP_UBFE                             = 0x8a,
    VKD3D_SM5_OP_IBFE                             = 0x8b,
    VKD3D_SM5_OP_BFI                              = 0x8c,
    VKD3D_SM5_OP_BFREV                            = 0x8d,
    VKD3D_SM5_OP_SWAPC                            = 0x8e,
    VKD3D_SM5_OP_DCL_STREAM                       = 0x8f,
    VKD3D_SM5_OP_DCL_FUNCTION_BODY                = 0x90,
    VKD3D_SM5_OP_DCL_FUNCTION_TABLE               = 0x91,
    VKD3D_SM5_OP_DCL_INTERFACE                    = 0x92,
    VKD3D_SM5_OP_DCL_INPUT_CONTROL_POINT_COUNT    = 0x93,
    VKD3D_SM5_OP_DCL_OUTPUT_CONTROL_POINT_COUNT   = 0x94,
    VKD3D_SM5_OP_DCL_TESSELLATOR_DOMAIN           = 0x95,
    VKD3D_SM5_OP_DCL_TESSELLATOR_PARTITIONING     = 0x96,
    VKD3D_SM5_OP_DCL_TESSELLATOR_OUTPUT_PRIMITIVE = 0x97,
    VKD3D_SM5_OP_DCL_HS_MAX_TESSFACTOR            = 0x98,
    VKD3D_SM5_OP_DCL_HS_FORK_PHASE_INSTANCE_COUNT = 0x99,
    VKD3D_SM5_OP_DCL_HS_JOIN_PHASE_INSTANCE_COUNT = 0x9a,
    VKD3D_SM5_OP_DCL_THREAD_GROUP                 = 0x9b,
    VKD3D_SM5_OP_DCL_UAV_TYPED                    = 0x9c,
    VKD3D_SM5_OP_DCL_UAV_RAW                      = 0x9d,
    VKD3D_SM5_OP_DCL_UAV_STRUCTURED               = 0x9e,
    VKD3D_SM5_OP_DCL_TGSM_RAW                     = 0x9f,
    VKD3D_SM5_OP_DCL_TGSM_STRUCTURED              = 0xa0,
    VKD3D_SM5_OP_DCL_RESOURCE_RAW                 = 0xa1,
    VKD3D_SM5_OP_DCL_RESOURCE_STRUCTURED          = 0xa2,
    VKD3D_SM5_OP_LD_UAV_TYPED                     = 0xa3,
    VKD3D_SM5_OP_STORE_UAV_TYPED                  = 0xa4,
    VKD3D_SM5_OP_LD_RAW                           = 0xa5,
    VKD3D_SM5_OP_STORE_RAW                        = 0xa6,
    VKD3D_SM5_OP_LD_STRUCTURED                    = 0xa7,
    VKD3D_SM5_OP_STORE_STRUCTURED                 = 0xa8,
    VKD3D_SM5_OP_ATOMIC_AND                       = 0xa9,
    VKD3D_SM5_OP_ATOMIC_OR                        = 0xaa,
    VKD3D_SM5_OP_ATOMIC_XOR                       = 0xab,
    VKD3D_SM5_OP_ATOMIC_CMP_STORE                 = 0xac,
    VKD3D_SM5_OP_ATOMIC_IADD                      = 0xad,
    VKD3D_SM5_OP_ATOMIC_IMAX                      = 0xae,
    VKD3D_SM5_OP_ATOMIC_IMIN                      = 0xaf,
    VKD3D_SM5_OP_ATOMIC_UMAX                      = 0xb0,
    VKD3D_SM5_OP_ATOMIC_UMIN                      = 0xb1,
    VKD3D_SM5_OP_IMM_ATOMIC_ALLOC                 = 0xb2,
    VKD3D_SM5_OP_IMM_ATOMIC_CONSUME               = 0xb3,
    VKD3D_SM5_OP_IMM_ATOMIC_IADD                  = 0xb4,
    VKD3D_SM5_OP_IMM_ATOMIC_AND                   = 0xb5,
    VKD3D_SM5_OP_IMM_ATOMIC_OR                    = 0xb6,
    VKD3D_SM5_OP_IMM_ATOMIC_XOR                   = 0xb7,
    VKD3D_SM5_OP_IMM_ATOMIC_EXCH                  = 0xb8,
    VKD3D_SM5_OP_IMM_ATOMIC_CMP_EXCH              = 0xb9,
    VKD3D_SM5_OP_IMM_ATOMIC_IMAX                  = 0xba,
    VKD3D_SM5_OP_IMM_ATOMIC_IMIN                  = 0xbb,
    VKD3D_SM5_OP_IMM_ATOMIC_UMAX                  = 0xbc,
    VKD3D_SM5_OP_IMM_ATOMIC_UMIN                  = 0xbd,
    VKD3D_SM5_OP_SYNC                             = 0xbe,
    VKD3D_SM5_OP_DADD                             = 0xbf,
    VKD3D_SM5_OP_DMAX                             = 0xc0,
    VKD3D_SM5_OP_DMIN                             = 0xc1,
    VKD3D_SM5_OP_DMUL                             = 0xc2,
    VKD3D_SM5_OP_DEQ                              = 0xc3,
    VKD3D_SM5_OP_DGE                              = 0xc4,
    VKD3D_SM5_OP_DLT                              = 0xc5,
    VKD3D_SM5_OP_DNE                              = 0xc6,
    VKD3D_SM5_OP_DMOV                             = 0xc7,
    VKD3D_SM5_OP_DMOVC                            = 0xc8,
    VKD3D_SM5_OP_DTOF                             = 0xc9,
    VKD3D_SM5_OP_FTOD                             = 0xca,
    VKD3D_SM5_OP_EVAL_SAMPLE_INDEX                = 0xcc,
    VKD3D_SM5_OP_EVAL_CENTROID                    = 0xcd,
    VKD3D_SM5_OP_DCL_GS_INSTANCES                 = 0xce,
    VKD3D_SM5_OP_DDIV                             = 0xd2,
    VKD3D_SM5_OP_DFMA                             = 0xd3,
    VKD3D_SM5_OP_DRCP                             = 0xd4,
    VKD3D_SM5_OP_MSAD                             = 0xd5,
    VKD3D_SM5_OP_DTOI                             = 0xd6,
    VKD3D_SM5_OP_DTOU                             = 0xd7,
    VKD3D_SM5_OP_ITOD                             = 0xd8,
    VKD3D_SM5_OP_UTOD                             = 0xd9,
    VKD3D_SM5_OP_GATHER4_S                        = 0xdb,
    VKD3D_SM5_OP_GATHER4_C_S                      = 0xdc,
    VKD3D_SM5_OP_GATHER4_PO_S                     = 0xdd,
    VKD3D_SM5_OP_GATHER4_PO_C_S                   = 0xde,
    VKD3D_SM5_OP_LD_S                             = 0xdf,
    VKD3D_SM5_OP_LD2DMS_S                         = 0xe0,
    VKD3D_SM5_OP_LD_UAV_TYPED_S                   = 0xe1,
    VKD3D_SM5_OP_LD_RAW_S                         = 0xe2,
    VKD3D_SM5_OP_LD_STRUCTURED_S                  = 0xe3,
    VKD3D_SM5_OP_SAMPLE_LOD_S                     = 0xe4,
    VKD3D_SM5_OP_SAMPLE_C_LZ_S                    = 0xe5,
    VKD3D_SM5_OP_SAMPLE_CL_S                      = 0xe6,
    VKD3D_SM5_OP_SAMPLE_B_CL_S                    = 0xe7,
    VKD3D_SM5_OP_SAMPLE_GRAD_CL_S                 = 0xe8,
    VKD3D_SM5_OP_SAMPLE_C_CL_S                    = 0xe9,
    VKD3D_SM5_OP_CHECK_ACCESS_FULLY_MAPPED        = 0xea,
};

enum vkd3d_sm4_instruction_modifier
{
    VKD3D_SM4_MODIFIER_AOFFIMMI         = 0x1,
    VKD3D_SM5_MODIFIER_RESOURCE_TYPE    = 0x2,
    VKD3D_SM5_MODIFIER_DATA_TYPE        = 0x3,
};

enum vkd3d_sm4_register_type
{
    VKD3D_SM4_RT_TEMP                    = 0x00,
    VKD3D_SM4_RT_INPUT                   = 0x01,
    VKD3D_SM4_RT_OUTPUT                  = 0x02,
    VKD3D_SM4_RT_INDEXABLE_TEMP          = 0x03,
    VKD3D_SM4_RT_IMMCONST                = 0x04,
    VKD3D_SM4_RT_IMMCONST64              = 0x05,
    VKD3D_SM4_RT_SAMPLER                 = 0x06,
    VKD3D_SM4_RT_RESOURCE                = 0x07,
    VKD3D_SM4_RT_CONSTBUFFER             = 0x08,
    VKD3D_SM4_RT_IMMCONSTBUFFER          = 0x09,
    VKD3D_SM4_RT_PRIMID                  = 0x0b,
    VKD3D_SM4_RT_DEPTHOUT                = 0x0c,
    VKD3D_SM4_RT_NULL                    = 0x0d,
    VKD3D_SM4_RT_RASTERIZER              = 0x0e,
    VKD3D_SM4_RT_OMASK                   = 0x0f,
    VKD3D_SM5_RT_STREAM                  = 0x10,
    VKD3D_SM5_RT_FUNCTION_BODY           = 0x11,
    VKD3D_SM5_RT_FUNCTION_POINTER        = 0x13,
    VKD3D_SM5_RT_OUTPUT_CONTROL_POINT_ID = 0x16,
    VKD3D_SM5_RT_FORK_INSTANCE_ID        = 0x17,
    VKD3D_SM5_RT_JOIN_INSTANCE_ID        = 0x18,
    VKD3D_SM5_RT_INPUT_CONTROL_POINT     = 0x19,
    VKD3D_SM5_RT_OUTPUT_CONTROL_POINT    = 0x1a,
    VKD3D_SM5_RT_PATCH_CONSTANT_DATA     = 0x1b,
    VKD3D_SM5_RT_DOMAIN_LOCATION         = 0x1c,
    VKD3D_SM5_RT_UAV                     = 0x1e,
    VKD3D_SM5_RT_SHARED_MEMORY           = 0x1f,
    VKD3D_SM5_RT_THREAD_ID               = 0x20,
    VKD3D_SM5_RT_THREAD_GROUP_ID         = 0x21,
    VKD3D_SM5_RT_LOCAL_THREAD_ID         = 0x22,
    VKD3D_SM5_RT_COVERAGE                = 0x23,
    VKD3D_SM5_RT_LOCAL_THREAD_INDEX      = 0x24,
    VKD3D_SM5_RT_GS_INSTANCE_ID          = 0x25,
    VKD3D_SM5_RT_DEPTHOUT_GREATER_EQUAL  = 0x26,
    VKD3D_SM5_RT_DEPTHOUT_LESS_EQUAL     = 0x27,
    VKD3D_SM5_RT_OUTPUT_STENCIL_REF      = 0x29,
};

enum vkd3d_sm4_extended_operand_type
{
    VKD3D_SM4_EXTENDED_OPERAND_NONE      = 0x0,
    VKD3D_SM4_EXTENDED_OPERAND_MODIFIER  = 0x1,
};

enum vkd3d_sm4_register_modifier
{
    VKD3D_SM4_REGISTER_MODIFIER_NONE       = 0x00,
    VKD3D_SM4_REGISTER_MODIFIER_NEGATE     = 0x01,
    VKD3D_SM4_REGISTER_MODIFIER_ABS        = 0x02,
    VKD3D_SM4_REGISTER_MODIFIER_ABS_NEGATE = 0x03,
};

enum vkd3d_sm4_register_precision
{
    VKD3D_SM4_REGISTER_PRECISION_DEFAULT      = 0x0,
    VKD3D_SM4_REGISTER_PRECISION_MIN_FLOAT_16 = 0x1,
    VKD3D_SM4_REGISTER_PRECISION_MIN_FLOAT_10 = 0x2,
    VKD3D_SM4_REGISTER_PRECISION_MIN_INT_16   = 0x4,
    VKD3D_SM4_REGISTER_PRECISION_MIN_UINT_16  = 0x5,
};

enum vkd3d_sm4_output_primitive_type
{
    VKD3D_SM4_OUTPUT_PT_POINTLIST     = 0x1,
    VKD3D_SM4_OUTPUT_PT_LINESTRIP     = 0x3,
    VKD3D_SM4_OUTPUT_PT_TRIANGLESTRIP = 0x5,
};

enum vkd3d_sm4_input_primitive_type
{
    VKD3D_SM4_INPUT_PT_POINT          = 0x01,
    VKD3D_SM4_INPUT_PT_LINE           = 0x02,
    VKD3D_SM4_INPUT_PT_TRIANGLE       = 0x03,
    VKD3D_SM4_INPUT_PT_LINEADJ        = 0x06,
    VKD3D_SM4_INPUT_PT_TRIANGLEADJ    = 0x07,
    VKD3D_SM5_INPUT_PT_PATCH1         = 0x08,
    VKD3D_SM5_INPUT_PT_PATCH2         = 0x09,
    VKD3D_SM5_INPUT_PT_PATCH3         = 0x0a,
    VKD3D_SM5_INPUT_PT_PATCH4         = 0x0b,
    VKD3D_SM5_INPUT_PT_PATCH5         = 0x0c,
    VKD3D_SM5_INPUT_PT_PATCH6         = 0x0d,
    VKD3D_SM5_INPUT_PT_PATCH7         = 0x0e,
    VKD3D_SM5_INPUT_PT_PATCH8         = 0x0f,
    VKD3D_SM5_INPUT_PT_PATCH9         = 0x10,
    VKD3D_SM5_INPUT_PT_PATCH10        = 0x11,
    VKD3D_SM5_INPUT_PT_PATCH11        = 0x12,
    VKD3D_SM5_INPUT_PT_PATCH12        = 0x13,
    VKD3D_SM5_INPUT_PT_PATCH13        = 0x14,
    VKD3D_SM5_INPUT_PT_PATCH14        = 0x15,
    VKD3D_SM5_INPUT_PT_PATCH15        = 0x16,
    VKD3D_SM5_INPUT_PT_PATCH16        = 0x17,
    VKD3D_SM5_INPUT_PT_PATCH17        = 0x18,
    VKD3D_SM5_INPUT_PT_PATCH18        = 0x19,
    VKD3D_SM5_INPUT_PT_PATCH19        = 0x1a,
    VKD3D_SM5_INPUT_PT_PATCH20        = 0x1b,
    VKD3D_SM5_INPUT_PT_PATCH21        = 0x1c,
    VKD3D_SM5_INPUT_PT_PATCH22        = 0x1d,
    VKD3D_SM5_INPUT_PT_PATCH23        = 0x1e,
    VKD3D_SM5_INPUT_PT_PATCH24        = 0x1f,
    VKD3D_SM5_INPUT_PT_PATCH25        = 0x20,
    VKD3D_SM5_INPUT_PT_PATCH26        = 0x21,
    VKD3D_SM5_INPUT_PT_PATCH27        = 0x22,
    VKD3D_SM5_INPUT_PT_PATCH28        = 0x23,
    VKD3D_SM5_INPUT_PT_PATCH29        = 0x24,
    VKD3D_SM5_INPUT_PT_PATCH30        = 0x25,
    VKD3D_SM5_INPUT_PT_PATCH31        = 0x26,
    VKD3D_SM5_INPUT_PT_PATCH32        = 0x27,
};

enum vkd3d_sm4_swizzle_type
{
    VKD3D_SM4_SWIZZLE_NONE            = 0x0,
    VKD3D_SM4_SWIZZLE_VEC4            = 0x1,
    VKD3D_SM4_SWIZZLE_SCALAR          = 0x2,
};

enum vkd3d_sm4_dimension
{
    VKD3D_SM4_DIMENSION_NONE    = 0x0,
    VKD3D_SM4_DIMENSION_SCALAR  = 0x1,
    VKD3D_SM4_DIMENSION_VEC4    = 0x2,
};

enum vkd3d_sm4_resource_type
{
    VKD3D_SM4_RESOURCE_BUFFER             = 0x1,
    VKD3D_SM4_RESOURCE_TEXTURE_1D         = 0x2,
    VKD3D_SM4_RESOURCE_TEXTURE_2D         = 0x3,
    VKD3D_SM4_RESOURCE_TEXTURE_2DMS       = 0x4,
    VKD3D_SM4_RESOURCE_TEXTURE_3D         = 0x5,
    VKD3D_SM4_RESOURCE_TEXTURE_CUBE       = 0x6,
    VKD3D_SM4_RESOURCE_TEXTURE_1DARRAY    = 0x7,
    VKD3D_SM4_RESOURCE_TEXTURE_2DARRAY    = 0x8,
    VKD3D_SM4_RESOURCE_TEXTURE_2DMSARRAY  = 0x9,
    VKD3D_SM4_RESOURCE_TEXTURE_CUBEARRAY  = 0xa,
    VKD3D_SM4_RESOURCE_RAW_BUFFER         = 0xb,
    VKD3D_SM4_RESOURCE_STRUCTURED_BUFFER  = 0xc,
};

enum vkd3d_sm4_data_type
{
    VKD3D_SM4_DATA_UNORM     = 0x1,
    VKD3D_SM4_DATA_SNORM     = 0x2,
    VKD3D_SM4_DATA_INT       = 0x3,
    VKD3D_SM4_DATA_UINT      = 0x4,
    VKD3D_SM4_DATA_FLOAT     = 0x5,
    VKD3D_SM4_DATA_MIXED     = 0x6,
    VKD3D_SM4_DATA_DOUBLE    = 0x7,
    VKD3D_SM4_DATA_CONTINUED = 0x8,
    VKD3D_SM4_DATA_UNUSED    = 0x9,
};

enum vkd3d_sm4_sampler_mode
{
    VKD3D_SM4_SAMPLER_DEFAULT    = 0x0,
    VKD3D_SM4_SAMPLER_COMPARISON = 0x1,
};

enum vkd3d_sm4_shader_data_type
{
    VKD3D_SM4_SHADER_DATA_IMMEDIATE_CONSTANT_BUFFER = 0x3,
    VKD3D_SM4_SHADER_DATA_MESSAGE                   = 0x4,
};

struct vkd3d_shader_sm4_parser
{
    const uint32_t *start, *end, *ptr;

    unsigned int output_map[MAX_REG_OUTPUT];

    struct vkd3d_shader_parser p;
};

struct vkd3d_sm4_opcode_info
{
    enum vkd3d_sm4_opcode opcode;
    enum vkd3d_shader_opcode handler_idx;
    char dst_info[SM4_MAX_DST_COUNT];
    char src_info[SM4_MAX_SRC_COUNT];
    void (*read_opcode_func)(struct vkd3d_shader_instruction *ins, uint32_t opcode, uint32_t opcode_token,
            const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv);
};

static const enum vkd3d_primitive_type output_primitive_type_table[] =
{
    /* UNKNOWN */                             VKD3D_PT_UNDEFINED,
    /* VKD3D_SM4_OUTPUT_PT_POINTLIST */       VKD3D_PT_POINTLIST,
    /* UNKNOWN */                             VKD3D_PT_UNDEFINED,
    /* VKD3D_SM4_OUTPUT_PT_LINESTRIP */       VKD3D_PT_LINESTRIP,
    /* UNKNOWN */                             VKD3D_PT_UNDEFINED,
    /* VKD3D_SM4_OUTPUT_PT_TRIANGLESTRIP */   VKD3D_PT_TRIANGLESTRIP,
};

static const enum vkd3d_primitive_type input_primitive_type_table[] =
{
    /* UNKNOWN */                             VKD3D_PT_UNDEFINED,
    /* VKD3D_SM4_INPUT_PT_POINT */            VKD3D_PT_POINTLIST,
    /* VKD3D_SM4_INPUT_PT_LINE */             VKD3D_PT_LINELIST,
    /* VKD3D_SM4_INPUT_PT_TRIANGLE */         VKD3D_PT_TRIANGLELIST,
    /* UNKNOWN */                             VKD3D_PT_UNDEFINED,
    /* UNKNOWN */                             VKD3D_PT_UNDEFINED,
    /* VKD3D_SM4_INPUT_PT_LINEADJ */          VKD3D_PT_LINELIST_ADJ,
    /* VKD3D_SM4_INPUT_PT_TRIANGLEADJ */      VKD3D_PT_TRIANGLELIST_ADJ,
};

static const enum vkd3d_shader_resource_type resource_type_table[] =
{
    /* 0 */                                       VKD3D_SHADER_RESOURCE_NONE,
    /* VKD3D_SM4_RESOURCE_BUFFER */               VKD3D_SHADER_RESOURCE_BUFFER,
    /* VKD3D_SM4_RESOURCE_TEXTURE_1D */           VKD3D_SHADER_RESOURCE_TEXTURE_1D,
    /* VKD3D_SM4_RESOURCE_TEXTURE_2D */           VKD3D_SHADER_RESOURCE_TEXTURE_2D,
    /* VKD3D_SM4_RESOURCE_TEXTURE_2DMS */         VKD3D_SHADER_RESOURCE_TEXTURE_2DMS,
    /* VKD3D_SM4_RESOURCE_TEXTURE_3D */           VKD3D_SHADER_RESOURCE_TEXTURE_3D,
    /* VKD3D_SM4_RESOURCE_TEXTURE_CUBE */         VKD3D_SHADER_RESOURCE_TEXTURE_CUBE,
    /* VKD3D_SM4_RESOURCE_TEXTURE_1DARRAY */      VKD3D_SHADER_RESOURCE_TEXTURE_1DARRAY,
    /* VKD3D_SM4_RESOURCE_TEXTURE_2DARRAY */      VKD3D_SHADER_RESOURCE_TEXTURE_2DARRAY,
    /* VKD3D_SM4_RESOURCE_TEXTURE_2DMSARRAY */    VKD3D_SHADER_RESOURCE_TEXTURE_2DMSARRAY,
    /* VKD3D_SM4_RESOURCE_TEXTURE_CUBEARRAY */    VKD3D_SHADER_RESOURCE_TEXTURE_CUBEARRAY,
    /* VKD3D_SM4_RESOURCE_RAW_BUFFER */           VKD3D_SHADER_RESOURCE_BUFFER,
    /* VKD3D_SM4_RESOURCE_STRUCTURED_BUFFER */    VKD3D_SHADER_RESOURCE_BUFFER,
};

static const enum vkd3d_data_type data_type_table[] =
{
    /* 0 */                         VKD3D_DATA_FLOAT,
    /* VKD3D_SM4_DATA_UNORM */      VKD3D_DATA_UNORM,
    /* VKD3D_SM4_DATA_SNORM */      VKD3D_DATA_SNORM,
    /* VKD3D_SM4_DATA_INT */        VKD3D_DATA_INT,
    /* VKD3D_SM4_DATA_UINT */       VKD3D_DATA_UINT,
    /* VKD3D_SM4_DATA_FLOAT */      VKD3D_DATA_FLOAT,
    /* VKD3D_SM4_DATA_MIXED */      VKD3D_DATA_MIXED,
    /* VKD3D_SM4_DATA_DOUBLE */     VKD3D_DATA_DOUBLE,
    /* VKD3D_SM4_DATA_CONTINUED */  VKD3D_DATA_CONTINUED,
    /* VKD3D_SM4_DATA_UNUSED */     VKD3D_DATA_UNUSED,
};

static struct vkd3d_shader_sm4_parser *vkd3d_shader_sm4_parser(struct vkd3d_shader_parser *parser)
{
    return CONTAINING_RECORD(parser, struct vkd3d_shader_sm4_parser, p);
}

static bool shader_is_sm_5_1(const struct vkd3d_shader_sm4_parser *sm4)
{
    const struct vkd3d_shader_version *version = &sm4->p.shader_version;

    return version->major >= 5 && version->minor >= 1;
}

static bool shader_sm4_read_src_param(struct vkd3d_shader_sm4_parser *priv, const uint32_t **ptr,
        const uint32_t *end, enum vkd3d_data_type data_type, struct vkd3d_shader_src_param *src_param);
static bool shader_sm4_read_dst_param(struct vkd3d_shader_sm4_parser *priv, const uint32_t **ptr,
        const uint32_t *end, enum vkd3d_data_type data_type, struct vkd3d_shader_dst_param *dst_param);

static bool shader_sm4_read_register_space(struct vkd3d_shader_sm4_parser *priv,
        const uint32_t **ptr, const uint32_t *end, unsigned int *register_space)
{
    *register_space = 0;

    if (!shader_is_sm_5_1(priv))
        return true;

    if (*ptr >= end)
    {
        WARN("Invalid ptr %p >= end %p.\n", *ptr, end);
        return false;
    }

    *register_space = *(*ptr)++;
    return true;
}

static void shader_sm4_read_conditional_op(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    shader_sm4_read_src_param(priv, &tokens, &tokens[token_count], VKD3D_DATA_UINT,
            (struct vkd3d_shader_src_param *)&ins->src[0]);
    ins->flags = (opcode_token & VKD3D_SM4_CONDITIONAL_NZ) ?
            VKD3D_SHADER_CONDITIONAL_OP_NZ : VKD3D_SHADER_CONDITIONAL_OP_Z;
}

static void shader_sm4_read_shader_data(struct vkd3d_shader_instruction *ins, uint32_t opcode, uint32_t opcode_token,
        const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    struct vkd3d_shader_immediate_constant_buffer *icb;
    enum vkd3d_sm4_shader_data_type type;
    unsigned int icb_size;

    type = (opcode_token & VKD3D_SM4_SHADER_DATA_TYPE_MASK) >> VKD3D_SM4_SHADER_DATA_TYPE_SHIFT;
    if (type != VKD3D_SM4_SHADER_DATA_IMMEDIATE_CONSTANT_BUFFER)
    {
        FIXME("Ignoring shader data type %#x.\n", type);
        ins->handler_idx = VKD3DSIH_NOP;
        return;
    }

    ++tokens;
    icb_size = token_count - 1;
    if (icb_size % 4)
    {
        FIXME("Unexpected immediate constant buffer size %u.\n", icb_size);
        ins->handler_idx = VKD3DSIH_INVALID;
        return;
    }

    if (!(icb = vkd3d_malloc(offsetof(struct vkd3d_shader_immediate_constant_buffer, data[icb_size]))))
    {
        ERR("Failed to allocate immediate constant buffer, size %u.\n", icb_size);
        vkd3d_shader_parser_error(&priv->p, VKD3D_SHADER_ERROR_TPF_OUT_OF_MEMORY, "Out of memory.");
        ins->handler_idx = VKD3DSIH_INVALID;
        return;
    }
    icb->vec4_count = icb_size / 4;
    memcpy(icb->data, tokens, sizeof(*tokens) * icb_size);
    shader_instruction_array_add_icb(&priv->p.instructions, icb);
    ins->declaration.icb = icb;
}

static void shader_sm4_set_descriptor_register_range(struct vkd3d_shader_sm4_parser *sm4,
        const struct vkd3d_shader_register *reg, struct vkd3d_shader_register_range *range)
{
    range->first = reg->idx[1].offset;
    range->last = reg->idx[shader_is_sm_5_1(sm4) ? 2 : 1].offset;
    if (range->last < range->first)
    {
        FIXME("Invalid register range [%u:%u].\n", range->first, range->last);
        vkd3d_shader_parser_error(&sm4->p, VKD3D_SHADER_ERROR_TPF_INVALID_REGISTER_RANGE,
                "Last register %u must not be less than first register %u in range.\n", range->last, range->first);
    }
}

static void shader_sm4_read_dcl_resource(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    struct vkd3d_shader_semantic *semantic = &ins->declaration.semantic;
    enum vkd3d_sm4_resource_type resource_type;
    const uint32_t *end = &tokens[token_count];
    enum vkd3d_sm4_data_type data_type;
    enum vkd3d_data_type reg_data_type;
    DWORD components;
    unsigned int i;

    resource_type = (opcode_token & VKD3D_SM4_RESOURCE_TYPE_MASK) >> VKD3D_SM4_RESOURCE_TYPE_SHIFT;
    if (!resource_type || (resource_type >= ARRAY_SIZE(resource_type_table)))
    {
        FIXME("Unhandled resource type %#x.\n", resource_type);
        semantic->resource_type = VKD3D_SHADER_RESOURCE_NONE;
    }
    else
    {
        semantic->resource_type = resource_type_table[resource_type];
    }

    if (semantic->resource_type == VKD3D_SHADER_RESOURCE_TEXTURE_2DMS
            || semantic->resource_type == VKD3D_SHADER_RESOURCE_TEXTURE_2DMSARRAY)
    {
        semantic->sample_count = (opcode_token & VKD3D_SM4_RESOURCE_SAMPLE_COUNT_MASK)
                >> VKD3D_SM4_RESOURCE_SAMPLE_COUNT_SHIFT;
    }

    reg_data_type = opcode == VKD3D_SM4_OP_DCL_RESOURCE ? VKD3D_DATA_RESOURCE : VKD3D_DATA_UAV;
    shader_sm4_read_dst_param(priv, &tokens, end, reg_data_type, &semantic->resource.reg);
    shader_sm4_set_descriptor_register_range(priv, &semantic->resource.reg.reg, &semantic->resource.range);

    components = *tokens++;
    for (i = 0; i < VKD3D_VEC4_SIZE; i++)
    {
        data_type = VKD3D_SM4_TYPE_COMPONENT(components, i);

        if (!data_type || (data_type >= ARRAY_SIZE(data_type_table)))
        {
            FIXME("Unhandled data type %#x.\n", data_type);
            semantic->resource_data_type[i] = VKD3D_DATA_FLOAT;
        }
        else
        {
            semantic->resource_data_type[i] = data_type_table[data_type];
        }
    }

    if (reg_data_type == VKD3D_DATA_UAV)
        ins->flags = (opcode_token & VKD3D_SM5_UAV_FLAGS_MASK) >> VKD3D_SM5_UAV_FLAGS_SHIFT;

    shader_sm4_read_register_space(priv, &tokens, end, &semantic->resource.range.space);
}

static void shader_sm4_read_dcl_constant_buffer(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    const uint32_t *end = &tokens[token_count];

    shader_sm4_read_src_param(priv, &tokens, end, VKD3D_DATA_FLOAT, &ins->declaration.cb.src);
    shader_sm4_set_descriptor_register_range(priv, &ins->declaration.cb.src.reg, &ins->declaration.cb.range);
    if (opcode_token & VKD3D_SM4_INDEX_TYPE_MASK)
        ins->flags |= VKD3DSI_INDEXED_DYNAMIC;

    ins->declaration.cb.size = ins->declaration.cb.src.reg.idx[2].offset;
    ins->declaration.cb.range.space = 0;

    if (shader_is_sm_5_1(priv))
    {
        if (tokens >= end)
        {
            FIXME("Invalid ptr %p >= end %p.\n", tokens, end);
            return;
        }

        ins->declaration.cb.size = *tokens++;
        shader_sm4_read_register_space(priv, &tokens, end, &ins->declaration.cb.range.space);
    }
}

static void shader_sm4_read_dcl_sampler(struct vkd3d_shader_instruction *ins, uint32_t opcode, uint32_t opcode_token,
        const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    const uint32_t *end = &tokens[token_count];

    ins->flags = (opcode_token & VKD3D_SM4_SAMPLER_MODE_MASK) >> VKD3D_SM4_SAMPLER_MODE_SHIFT;
    if (ins->flags & ~VKD3D_SM4_SAMPLER_COMPARISON)
        FIXME("Unhandled sampler mode %#x.\n", ins->flags);
    shader_sm4_read_src_param(priv, &tokens, end, VKD3D_DATA_SAMPLER, &ins->declaration.sampler.src);
    shader_sm4_set_descriptor_register_range(priv, &ins->declaration.sampler.src.reg, &ins->declaration.sampler.range);
    shader_sm4_read_register_space(priv, &tokens, end, &ins->declaration.sampler.range.space);
}

static void shader_sm4_read_dcl_index_range(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    shader_sm4_read_dst_param(priv, &tokens, &tokens[token_count], VKD3D_DATA_OPAQUE,
            &ins->declaration.index_range.dst);
    ins->declaration.index_range.register_count = *tokens;
}

static void shader_sm4_read_dcl_output_topology(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    enum vkd3d_sm4_output_primitive_type primitive_type;

    primitive_type = (opcode_token & VKD3D_SM4_PRIMITIVE_TYPE_MASK) >> VKD3D_SM4_PRIMITIVE_TYPE_SHIFT;
    if (primitive_type >= ARRAY_SIZE(output_primitive_type_table))
        ins->declaration.primitive_type.type = VKD3D_PT_UNDEFINED;
    else
        ins->declaration.primitive_type.type = output_primitive_type_table[primitive_type];

    if (ins->declaration.primitive_type.type == VKD3D_PT_UNDEFINED)
        FIXME("Unhandled output primitive type %#x.\n", primitive_type);
}

static void shader_sm4_read_dcl_input_primitive(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    enum vkd3d_sm4_input_primitive_type primitive_type;

    primitive_type = (opcode_token & VKD3D_SM4_PRIMITIVE_TYPE_MASK) >> VKD3D_SM4_PRIMITIVE_TYPE_SHIFT;
    if (VKD3D_SM5_INPUT_PT_PATCH1 <= primitive_type && primitive_type <= VKD3D_SM5_INPUT_PT_PATCH32)
    {
        ins->declaration.primitive_type.type = VKD3D_PT_PATCH;
        ins->declaration.primitive_type.patch_vertex_count = primitive_type - VKD3D_SM5_INPUT_PT_PATCH1 + 1;
    }
    else if (primitive_type >= ARRAY_SIZE(input_primitive_type_table))
    {
        ins->declaration.primitive_type.type = VKD3D_PT_UNDEFINED;
    }
    else
    {
        ins->declaration.primitive_type.type = input_primitive_type_table[primitive_type];
    }

    if (ins->declaration.primitive_type.type == VKD3D_PT_UNDEFINED)
        FIXME("Unhandled input primitive type %#x.\n", primitive_type);
}

static void shader_sm4_read_declaration_count(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->declaration.count = *tokens;
}

static void shader_sm4_read_declaration_dst(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    shader_sm4_read_dst_param(priv, &tokens, &tokens[token_count], VKD3D_DATA_FLOAT, &ins->declaration.dst);
}

static void shader_sm4_read_declaration_register_semantic(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    shader_sm4_read_dst_param(priv, &tokens, &tokens[token_count], VKD3D_DATA_FLOAT,
            &ins->declaration.register_semantic.reg);
    ins->declaration.register_semantic.sysval_semantic = *tokens;
}

static void shader_sm4_read_dcl_input_ps(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->flags = (opcode_token & VKD3D_SM4_INTERPOLATION_MODE_MASK) >> VKD3D_SM4_INTERPOLATION_MODE_SHIFT;
    shader_sm4_read_dst_param(priv, &tokens, &tokens[token_count], VKD3D_DATA_FLOAT, &ins->declaration.dst);
}

static void shader_sm4_read_dcl_input_ps_siv(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->flags = (opcode_token & VKD3D_SM4_INTERPOLATION_MODE_MASK) >> VKD3D_SM4_INTERPOLATION_MODE_SHIFT;
    shader_sm4_read_dst_param(priv, &tokens, &tokens[token_count], VKD3D_DATA_FLOAT,
            &ins->declaration.register_semantic.reg);
    ins->declaration.register_semantic.sysval_semantic = *tokens;
}

static void shader_sm4_read_dcl_indexable_temp(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->declaration.indexable_temp.register_idx = *tokens++;
    ins->declaration.indexable_temp.register_size = *tokens++;
    ins->declaration.indexable_temp.component_count = *tokens;
}

static void shader_sm4_read_dcl_global_flags(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->flags = (opcode_token & VKD3D_SM4_GLOBAL_FLAGS_MASK) >> VKD3D_SM4_GLOBAL_FLAGS_SHIFT;
}

static void shader_sm5_read_fcall(struct vkd3d_shader_instruction *ins, uint32_t opcode, uint32_t opcode_token,
        const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    struct vkd3d_shader_src_param *src_params = (struct vkd3d_shader_src_param *)ins->src;
    src_params[0].reg.u.fp_body_idx = *tokens++;
    shader_sm4_read_src_param(priv, &tokens, &tokens[token_count], VKD3D_DATA_OPAQUE, &src_params[0]);
}

static void shader_sm5_read_dcl_function_body(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->declaration.index = *tokens;
}

static void shader_sm5_read_dcl_function_table(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->declaration.index = *tokens++;
    FIXME("Ignoring set of function bodies (count %u).\n", *tokens);
}

static void shader_sm5_read_dcl_interface(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->declaration.fp.index = *tokens++;
    ins->declaration.fp.body_count = *tokens++;
    ins->declaration.fp.array_size = *tokens >> VKD3D_SM5_FP_ARRAY_SIZE_SHIFT;
    ins->declaration.fp.table_count = *tokens++ & VKD3D_SM5_FP_TABLE_COUNT_MASK;
    FIXME("Ignoring set of function tables (count %u).\n", ins->declaration.fp.table_count);
}

static void shader_sm5_read_control_point_count(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->declaration.count = (opcode_token & VKD3D_SM5_CONTROL_POINT_COUNT_MASK)
            >> VKD3D_SM5_CONTROL_POINT_COUNT_SHIFT;
}

static void shader_sm5_read_dcl_tessellator_domain(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->declaration.tessellator_domain = (opcode_token & VKD3D_SM5_TESSELLATOR_MASK)
            >> VKD3D_SM5_TESSELLATOR_SHIFT;
}

static void shader_sm5_read_dcl_tessellator_partitioning(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->declaration.tessellator_partitioning = (opcode_token & VKD3D_SM5_TESSELLATOR_MASK)
            >> VKD3D_SM5_TESSELLATOR_SHIFT;
}

static void shader_sm5_read_dcl_tessellator_output_primitive(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->declaration.tessellator_output_primitive = (opcode_token & VKD3D_SM5_TESSELLATOR_MASK)
            >> VKD3D_SM5_TESSELLATOR_SHIFT;
}

static void shader_sm5_read_dcl_hs_max_tessfactor(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->declaration.max_tessellation_factor = *(float *)tokens;
}

static void shader_sm5_read_dcl_thread_group(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->declaration.thread_group_size.x = *tokens++;
    ins->declaration.thread_group_size.y = *tokens++;
    ins->declaration.thread_group_size.z = *tokens++;
}

static void shader_sm5_read_dcl_uav_raw(struct vkd3d_shader_instruction *ins, uint32_t opcode, uint32_t opcode_token,
        const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    struct vkd3d_shader_raw_resource *resource = &ins->declaration.raw_resource;
    const uint32_t *end = &tokens[token_count];

    shader_sm4_read_dst_param(priv, &tokens, end, VKD3D_DATA_UAV, &resource->resource.reg);
    shader_sm4_set_descriptor_register_range(priv, &resource->resource.reg.reg, &resource->resource.range);
    ins->flags = (opcode_token & VKD3D_SM5_UAV_FLAGS_MASK) >> VKD3D_SM5_UAV_FLAGS_SHIFT;
    shader_sm4_read_register_space(priv, &tokens, end, &resource->resource.range.space);
}

static void shader_sm5_read_dcl_uav_structured(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    struct vkd3d_shader_structured_resource *resource = &ins->declaration.structured_resource;
    const uint32_t *end = &tokens[token_count];

    shader_sm4_read_dst_param(priv, &tokens, end, VKD3D_DATA_UAV, &resource->resource.reg);
    shader_sm4_set_descriptor_register_range(priv, &resource->resource.reg.reg, &resource->resource.range);
    ins->flags = (opcode_token & VKD3D_SM5_UAV_FLAGS_MASK) >> VKD3D_SM5_UAV_FLAGS_SHIFT;
    resource->byte_stride = *tokens++;
    if (resource->byte_stride % 4)
        FIXME("Byte stride %u is not multiple of 4.\n", resource->byte_stride);
    shader_sm4_read_register_space(priv, &tokens, end, &resource->resource.range.space);
}

static void shader_sm5_read_dcl_tgsm_raw(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    shader_sm4_read_dst_param(priv, &tokens, &tokens[token_count], VKD3D_DATA_FLOAT, &ins->declaration.tgsm_raw.reg);
    ins->declaration.tgsm_raw.byte_count = *tokens;
    if (ins->declaration.tgsm_raw.byte_count % 4)
        FIXME("Byte count %u is not multiple of 4.\n", ins->declaration.tgsm_raw.byte_count);
}

static void shader_sm5_read_dcl_tgsm_structured(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    shader_sm4_read_dst_param(priv, &tokens, &tokens[token_count], VKD3D_DATA_FLOAT,
            &ins->declaration.tgsm_structured.reg);
    ins->declaration.tgsm_structured.byte_stride = *tokens++;
    ins->declaration.tgsm_structured.structure_count = *tokens;
    if (ins->declaration.tgsm_structured.byte_stride % 4)
        FIXME("Byte stride %u is not multiple of 4.\n", ins->declaration.tgsm_structured.byte_stride);
}

static void shader_sm5_read_dcl_resource_structured(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    struct vkd3d_shader_structured_resource *resource = &ins->declaration.structured_resource;
    const uint32_t *end = &tokens[token_count];

    shader_sm4_read_dst_param(priv, &tokens, end, VKD3D_DATA_RESOURCE, &resource->resource.reg);
    shader_sm4_set_descriptor_register_range(priv, &resource->resource.reg.reg, &resource->resource.range);
    resource->byte_stride = *tokens++;
    if (resource->byte_stride % 4)
        FIXME("Byte stride %u is not multiple of 4.\n", resource->byte_stride);
    shader_sm4_read_register_space(priv, &tokens, end, &resource->resource.range.space);
}

static void shader_sm5_read_dcl_resource_raw(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    struct vkd3d_shader_raw_resource *resource = &ins->declaration.raw_resource;
    const uint32_t *end = &tokens[token_count];

    shader_sm4_read_dst_param(priv, &tokens, end, VKD3D_DATA_RESOURCE, &resource->resource.reg);
    shader_sm4_set_descriptor_register_range(priv, &resource->resource.reg.reg, &resource->resource.range);
    shader_sm4_read_register_space(priv, &tokens, end, &resource->resource.range.space);
}

static void shader_sm5_read_sync(struct vkd3d_shader_instruction *ins, uint32_t opcode, uint32_t opcode_token,
        const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->flags = (opcode_token & VKD3D_SM5_SYNC_FLAGS_MASK) >> VKD3D_SM5_SYNC_FLAGS_SHIFT;
}

/*
 * d -> VKD3D_DATA_DOUBLE
 * f -> VKD3D_DATA_FLOAT
 * i -> VKD3D_DATA_INT
 * u -> VKD3D_DATA_UINT
 * O -> VKD3D_DATA_OPAQUE
 * R -> VKD3D_DATA_RESOURCE
 * S -> VKD3D_DATA_SAMPLER
 * U -> VKD3D_DATA_UAV
 */
static const struct vkd3d_sm4_opcode_info opcode_table[] =
{
    {VKD3D_SM4_OP_ADD,                              VKD3DSIH_ADD,                              "f",    "ff"},
    {VKD3D_SM4_OP_AND,                              VKD3DSIH_AND,                              "u",    "uu"},
    {VKD3D_SM4_OP_BREAK,                            VKD3DSIH_BREAK,                            "",     ""},
    {VKD3D_SM4_OP_BREAKC,                           VKD3DSIH_BREAKP,                           "",     "u",
            shader_sm4_read_conditional_op},
    {VKD3D_SM4_OP_CASE,                             VKD3DSIH_CASE,                             "",     "u"},
    {VKD3D_SM4_OP_CONTINUE,                         VKD3DSIH_CONTINUE,                         "",     ""},
    {VKD3D_SM4_OP_CONTINUEC,                        VKD3DSIH_CONTINUEP,                        "",     "u",
            shader_sm4_read_conditional_op},
    {VKD3D_SM4_OP_CUT,                              VKD3DSIH_CUT,                              "",     ""},
    {VKD3D_SM4_OP_DEFAULT,                          VKD3DSIH_DEFAULT,                          "",     ""},
    {VKD3D_SM4_OP_DERIV_RTX,                        VKD3DSIH_DSX,                              "f",    "f"},
    {VKD3D_SM4_OP_DERIV_RTY,                        VKD3DSIH_DSY,                              "f",    "f"},
    {VKD3D_SM4_OP_DISCARD,                          VKD3DSIH_DISCARD,                          "",     "u",
            shader_sm4_read_conditional_op},
    {VKD3D_SM4_OP_DIV,                              VKD3DSIH_DIV,                              "f",    "ff"},
    {VKD3D_SM4_OP_DP2,                              VKD3DSIH_DP2,                              "f",    "ff"},
    {VKD3D_SM4_OP_DP3,                              VKD3DSIH_DP3,                              "f",    "ff"},
    {VKD3D_SM4_OP_DP4,                              VKD3DSIH_DP4,                              "f",    "ff"},
    {VKD3D_SM4_OP_ELSE,                             VKD3DSIH_ELSE,                             "",     ""},
    {VKD3D_SM4_OP_EMIT,                             VKD3DSIH_EMIT,                             "",     ""},
    {VKD3D_SM4_OP_ENDIF,                            VKD3DSIH_ENDIF,                            "",     ""},
    {VKD3D_SM4_OP_ENDLOOP,                          VKD3DSIH_ENDLOOP,                          "",     ""},
    {VKD3D_SM4_OP_ENDSWITCH,                        VKD3DSIH_ENDSWITCH,                        "",     ""},
    {VKD3D_SM4_OP_EQ,                               VKD3DSIH_EQ,                               "u",    "ff"},
    {VKD3D_SM4_OP_EXP,                              VKD3DSIH_EXP,                              "f",    "f"},
    {VKD3D_SM4_OP_FRC,                              VKD3DSIH_FRC,                              "f",    "f"},
    {VKD3D_SM4_OP_FTOI,                             VKD3DSIH_FTOI,                             "i",    "f"},
    {VKD3D_SM4_OP_FTOU,                             VKD3DSIH_FTOU,                             "u",    "f"},
    {VKD3D_SM4_OP_GE,                               VKD3DSIH_GE,                               "u",    "ff"},
    {VKD3D_SM4_OP_IADD,                             VKD3DSIH_IADD,                             "i",    "ii"},
    {VKD3D_SM4_OP_IF,                               VKD3DSIH_IF,                               "",     "u",
            shader_sm4_read_conditional_op},
    {VKD3D_SM4_OP_IEQ,                              VKD3DSIH_IEQ,                              "u",    "ii"},
    {VKD3D_SM4_OP_IGE,                              VKD3DSIH_IGE,                              "u",    "ii"},
    {VKD3D_SM4_OP_ILT,                              VKD3DSIH_ILT,                              "u",    "ii"},
    {VKD3D_SM4_OP_IMAD,                             VKD3DSIH_IMAD,                             "i",    "iii"},
    {VKD3D_SM4_OP_IMAX,                             VKD3DSIH_IMAX,                             "i",    "ii"},
    {VKD3D_SM4_OP_IMIN,                             VKD3DSIH_IMIN,                             "i",    "ii"},
    {VKD3D_SM4_OP_IMUL,                             VKD3DSIH_IMUL,                             "ii",   "ii"},
    {VKD3D_SM4_OP_INE,                              VKD3DSIH_INE,                              "u",    "ii"},
    {VKD3D_SM4_OP_INEG,                             VKD3DSIH_INEG,                             "i",    "i"},
    {VKD3D_SM4_OP_ISHL,                             VKD3DSIH_ISHL,                             "i",    "ii"},
    {VKD3D_SM4_OP_ISHR,                             VKD3DSIH_ISHR,                             "i",    "ii"},
    {VKD3D_SM4_OP_ITOF,                             VKD3DSIH_ITOF,                             "f",    "i"},
    {VKD3D_SM4_OP_LABEL,                            VKD3DSIH_LABEL,                            "",     "O"},
    {VKD3D_SM4_OP_LD,                               VKD3DSIH_LD,                               "u",    "iR"},
    {VKD3D_SM4_OP_LD2DMS,                           VKD3DSIH_LD2DMS,                           "u",    "iRi"},
    {VKD3D_SM4_OP_LOG,                              VKD3DSIH_LOG,                              "f",    "f"},
    {VKD3D_SM4_OP_LOOP,                             VKD3DSIH_LOOP,                             "",     ""},
    {VKD3D_SM4_OP_LT,                               VKD3DSIH_LT,                               "u",    "ff"},
    {VKD3D_SM4_OP_MAD,                              VKD3DSIH_MAD,                              "f",    "fff"},
    {VKD3D_SM4_OP_MIN,                              VKD3DSIH_MIN,                              "f",    "ff"},
    {VKD3D_SM4_OP_MAX,                              VKD3DSIH_MAX,                              "f",    "ff"},
    {VKD3D_SM4_OP_SHADER_DATA,                      VKD3DSIH_DCL_IMMEDIATE_CONSTANT_BUFFER,    "",     "",
            shader_sm4_read_shader_data},
    {VKD3D_SM4_OP_MOV,                              VKD3DSIH_MOV,                              "f",    "f"},
    {VKD3D_SM4_OP_MOVC,                             VKD3DSIH_MOVC,                             "f",    "uff"},
    {VKD3D_SM4_OP_MUL,                              VKD3DSIH_MUL,                              "f",    "ff"},
    {VKD3D_SM4_OP_NE,                               VKD3DSIH_NE,                               "u",    "ff"},
    {VKD3D_SM4_OP_NOP,                              VKD3DSIH_NOP,                              "",     ""},
    {VKD3D_SM4_OP_NOT,                              VKD3DSIH_NOT,                              "u",    "u"},
    {VKD3D_SM4_OP_OR,                               VKD3DSIH_OR,                               "u",    "uu"},
    {VKD3D_SM4_OP_RESINFO,                          VKD3DSIH_RESINFO,                          "f",    "iR"},
    {VKD3D_SM4_OP_RET,                              VKD3DSIH_RET,                              "",     ""},
    {VKD3D_SM4_OP_RETC,                             VKD3DSIH_RETP,                             "",     "u",
            shader_sm4_read_conditional_op},
    {VKD3D_SM4_OP_ROUND_NE,                         VKD3DSIH_ROUND_NE,                         "f",    "f"},
    {VKD3D_SM4_OP_ROUND_NI,                         VKD3DSIH_ROUND_NI,                         "f",    "f"},
    {VKD3D_SM4_OP_ROUND_PI,                         VKD3DSIH_ROUND_PI,                         "f",    "f"},
    {VKD3D_SM4_OP_ROUND_Z,                          VKD3DSIH_ROUND_Z,                          "f",    "f"},
    {VKD3D_SM4_OP_RSQ,                              VKD3DSIH_RSQ,                              "f",    "f"},
    {VKD3D_SM4_OP_SAMPLE,                           VKD3DSIH_SAMPLE,                           "u",    "fRS"},
    {VKD3D_SM4_OP_SAMPLE_C,                         VKD3DSIH_SAMPLE_C,                         "f",    "fRSf"},
    {VKD3D_SM4_OP_SAMPLE_C_LZ,                      VKD3DSIH_SAMPLE_C_LZ,                      "f",    "fRSf"},
    {VKD3D_SM4_OP_SAMPLE_LOD,                       VKD3DSIH_SAMPLE_LOD,                       "u",    "fRSf"},
    {VKD3D_SM4_OP_SAMPLE_GRAD,                      VKD3DSIH_SAMPLE_GRAD,                      "u",    "fRSff"},
    {VKD3D_SM4_OP_SAMPLE_B,                         VKD3DSIH_SAMPLE_B,                         "u",    "fRSf"},
    {VKD3D_SM4_OP_SQRT,                             VKD3DSIH_SQRT,                             "f",    "f"},
    {VKD3D_SM4_OP_SWITCH,                           VKD3DSIH_SWITCH,                           "",     "i"},
    {VKD3D_SM4_OP_SINCOS,                           VKD3DSIH_SINCOS,                           "ff",   "f"},
    {VKD3D_SM4_OP_UDIV,                             VKD3DSIH_UDIV,                             "uu",   "uu"},
    {VKD3D_SM4_OP_ULT,                              VKD3DSIH_ULT,                              "u",    "uu"},
    {VKD3D_SM4_OP_UGE,                              VKD3DSIH_UGE,                              "u",    "uu"},
    {VKD3D_SM4_OP_UMUL,                             VKD3DSIH_UMUL,                             "uu",   "uu"},
    {VKD3D_SM4_OP_UMAX,                             VKD3DSIH_UMAX,                             "u",    "uu"},
    {VKD3D_SM4_OP_UMIN,                             VKD3DSIH_UMIN,                             "u",    "uu"},
    {VKD3D_SM4_OP_USHR,                             VKD3DSIH_USHR,                             "u",    "uu"},
    {VKD3D_SM4_OP_UTOF,                             VKD3DSIH_UTOF,                             "f",    "u"},
    {VKD3D_SM4_OP_XOR,                              VKD3DSIH_XOR,                              "u",    "uu"},
    {VKD3D_SM4_OP_DCL_RESOURCE,                     VKD3DSIH_DCL,                              "",     "",
            shader_sm4_read_dcl_resource},
    {VKD3D_SM4_OP_DCL_CONSTANT_BUFFER,              VKD3DSIH_DCL_CONSTANT_BUFFER,              "",     "",
            shader_sm4_read_dcl_constant_buffer},
    {VKD3D_SM4_OP_DCL_SAMPLER,                      VKD3DSIH_DCL_SAMPLER,                      "",     "",
            shader_sm4_read_dcl_sampler},
    {VKD3D_SM4_OP_DCL_INDEX_RANGE,                  VKD3DSIH_DCL_INDEX_RANGE,                  "",     "",
            shader_sm4_read_dcl_index_range},
    {VKD3D_SM4_OP_DCL_OUTPUT_TOPOLOGY,              VKD3DSIH_DCL_OUTPUT_TOPOLOGY,              "",     "",
            shader_sm4_read_dcl_output_topology},
    {VKD3D_SM4_OP_DCL_INPUT_PRIMITIVE,              VKD3DSIH_DCL_INPUT_PRIMITIVE,              "",     "",
            shader_sm4_read_dcl_input_primitive},
    {VKD3D_SM4_OP_DCL_VERTICES_OUT,                 VKD3DSIH_DCL_VERTICES_OUT,                 "",     "",
            shader_sm4_read_declaration_count},
    {VKD3D_SM4_OP_DCL_INPUT,                        VKD3DSIH_DCL_INPUT,                        "",     "",
            shader_sm4_read_declaration_dst},
    {VKD3D_SM4_OP_DCL_INPUT_SGV,                    VKD3DSIH_DCL_INPUT_SGV,                    "",     "",
            shader_sm4_read_declaration_register_semantic},
    {VKD3D_SM4_OP_DCL_INPUT_SIV,                    VKD3DSIH_DCL_INPUT_SIV,                    "",     "",
            shader_sm4_read_declaration_register_semantic},
    {VKD3D_SM4_OP_DCL_INPUT_PS,                     VKD3DSIH_DCL_INPUT_PS,                     "",     "",
            shader_sm4_read_dcl_input_ps},
    {VKD3D_SM4_OP_DCL_INPUT_PS_SGV,                 VKD3DSIH_DCL_INPUT_PS_SGV,                 "",     "",
            shader_sm4_read_declaration_register_semantic},
    {VKD3D_SM4_OP_DCL_INPUT_PS_SIV,                 VKD3DSIH_DCL_INPUT_PS_SIV,                 "",     "",
            shader_sm4_read_dcl_input_ps_siv},
    {VKD3D_SM4_OP_DCL_OUTPUT,                       VKD3DSIH_DCL_OUTPUT,                       "",     "",
            shader_sm4_read_declaration_dst},
    {VKD3D_SM4_OP_DCL_OUTPUT_SIV,                   VKD3DSIH_DCL_OUTPUT_SIV,                   "",     "",
            shader_sm4_read_declaration_register_semantic},
    {VKD3D_SM4_OP_DCL_TEMPS,                        VKD3DSIH_DCL_TEMPS,                        "",     "",
            shader_sm4_read_declaration_count},
    {VKD3D_SM4_OP_DCL_INDEXABLE_TEMP,               VKD3DSIH_DCL_INDEXABLE_TEMP,               "",     "",
            shader_sm4_read_dcl_indexable_temp},
    {VKD3D_SM4_OP_DCL_GLOBAL_FLAGS,                 VKD3DSIH_DCL_GLOBAL_FLAGS,                 "",     "",
            shader_sm4_read_dcl_global_flags},
    {VKD3D_SM4_OP_LOD,                              VKD3DSIH_LOD,                              "f",    "fRS"},
    {VKD3D_SM4_OP_GATHER4,                          VKD3DSIH_GATHER4,                          "u",    "fRS"},
    {VKD3D_SM4_OP_SAMPLE_POS,                       VKD3DSIH_SAMPLE_POS,                       "f",    "Ru"},
    {VKD3D_SM4_OP_SAMPLE_INFO,                      VKD3DSIH_SAMPLE_INFO,                      "f",    "R"},
    {VKD3D_SM5_OP_HS_DECLS,                         VKD3DSIH_HS_DECLS,                         "",     ""},
    {VKD3D_SM5_OP_HS_CONTROL_POINT_PHASE,           VKD3DSIH_HS_CONTROL_POINT_PHASE,           "",     ""},
    {VKD3D_SM5_OP_HS_FORK_PHASE,                    VKD3DSIH_HS_FORK_PHASE,                    "",     ""},
    {VKD3D_SM5_OP_HS_JOIN_PHASE,                    VKD3DSIH_HS_JOIN_PHASE,                    "",     ""},
    {VKD3D_SM5_OP_EMIT_STREAM,                      VKD3DSIH_EMIT_STREAM,                      "",     "f"},
    {VKD3D_SM5_OP_CUT_STREAM,                       VKD3DSIH_CUT_STREAM,                       "",     "f"},
    {VKD3D_SM5_OP_FCALL,                            VKD3DSIH_FCALL,                            "",     "O",
            shader_sm5_read_fcall},
    {VKD3D_SM5_OP_BUFINFO,                          VKD3DSIH_BUFINFO,                          "i",    "U"},
    {VKD3D_SM5_OP_DERIV_RTX_COARSE,                 VKD3DSIH_DSX_COARSE,                       "f",    "f"},
    {VKD3D_SM5_OP_DERIV_RTX_FINE,                   VKD3DSIH_DSX_FINE,                         "f",    "f"},
    {VKD3D_SM5_OP_DERIV_RTY_COARSE,                 VKD3DSIH_DSY_COARSE,                       "f",    "f"},
    {VKD3D_SM5_OP_DERIV_RTY_FINE,                   VKD3DSIH_DSY_FINE,                         "f",    "f"},
    {VKD3D_SM5_OP_GATHER4_C,                        VKD3DSIH_GATHER4_C,                        "f",    "fRSf"},
    {VKD3D_SM5_OP_GATHER4_PO,                       VKD3DSIH_GATHER4_PO,                       "f",    "fiRS"},
    {VKD3D_SM5_OP_GATHER4_PO_C,                     VKD3DSIH_GATHER4_PO_C,                     "f",    "fiRSf"},
    {VKD3D_SM5_OP_RCP,                              VKD3DSIH_RCP,                              "f",    "f"},
    {VKD3D_SM5_OP_F32TOF16,                         VKD3DSIH_F32TOF16,                         "u",    "f"},
    {VKD3D_SM5_OP_F16TOF32,                         VKD3DSIH_F16TOF32,                         "f",    "u"},
    {VKD3D_SM5_OP_COUNTBITS,                        VKD3DSIH_COUNTBITS,                        "u",    "u"},
    {VKD3D_SM5_OP_FIRSTBIT_HI,                      VKD3DSIH_FIRSTBIT_HI,                      "u",    "u"},
    {VKD3D_SM5_OP_FIRSTBIT_LO,                      VKD3DSIH_FIRSTBIT_LO,                      "u",    "u"},
    {VKD3D_SM5_OP_FIRSTBIT_SHI,                     VKD3DSIH_FIRSTBIT_SHI,                     "u",    "i"},
    {VKD3D_SM5_OP_UBFE,                             VKD3DSIH_UBFE,                             "u",    "iiu"},
    {VKD3D_SM5_OP_IBFE,                             VKD3DSIH_IBFE,                             "i",    "iii"},
    {VKD3D_SM5_OP_BFI,                              VKD3DSIH_BFI,                              "u",    "iiuu"},
    {VKD3D_SM5_OP_BFREV,                            VKD3DSIH_BFREV,                            "u",    "u"},
    {VKD3D_SM5_OP_SWAPC,                            VKD3DSIH_SWAPC,                            "ff",   "uff"},
    {VKD3D_SM5_OP_DCL_STREAM,                       VKD3DSIH_DCL_STREAM,                       "",     "O"},
    {VKD3D_SM5_OP_DCL_FUNCTION_BODY,                VKD3DSIH_DCL_FUNCTION_BODY,                "",     "",
            shader_sm5_read_dcl_function_body},
    {VKD3D_SM5_OP_DCL_FUNCTION_TABLE,               VKD3DSIH_DCL_FUNCTION_TABLE,               "",     "",
            shader_sm5_read_dcl_function_table},
    {VKD3D_SM5_OP_DCL_INTERFACE,                    VKD3DSIH_DCL_INTERFACE,                    "",     "",
            shader_sm5_read_dcl_interface},
    {VKD3D_SM5_OP_DCL_INPUT_CONTROL_POINT_COUNT,    VKD3DSIH_DCL_INPUT_CONTROL_POINT_COUNT,    "",     "",
            shader_sm5_read_control_point_count},
    {VKD3D_SM5_OP_DCL_OUTPUT_CONTROL_POINT_COUNT,   VKD3DSIH_DCL_OUTPUT_CONTROL_POINT_COUNT,   "",     "",
            shader_sm5_read_control_point_count},
    {VKD3D_SM5_OP_DCL_TESSELLATOR_DOMAIN,           VKD3DSIH_DCL_TESSELLATOR_DOMAIN,           "",     "",
            shader_sm5_read_dcl_tessellator_domain},
    {VKD3D_SM5_OP_DCL_TESSELLATOR_PARTITIONING,     VKD3DSIH_DCL_TESSELLATOR_PARTITIONING,     "",     "",
            shader_sm5_read_dcl_tessellator_partitioning},
    {VKD3D_SM5_OP_DCL_TESSELLATOR_OUTPUT_PRIMITIVE, VKD3DSIH_DCL_TESSELLATOR_OUTPUT_PRIMITIVE, "",     "",
            shader_sm5_read_dcl_tessellator_output_primitive},
    {VKD3D_SM5_OP_DCL_HS_MAX_TESSFACTOR,            VKD3DSIH_DCL_HS_MAX_TESSFACTOR,            "",     "",
            shader_sm5_read_dcl_hs_max_tessfactor},
    {VKD3D_SM5_OP_DCL_HS_FORK_PHASE_INSTANCE_COUNT, VKD3DSIH_DCL_HS_FORK_PHASE_INSTANCE_COUNT, "",     "",
            shader_sm4_read_declaration_count},
    {VKD3D_SM5_OP_DCL_HS_JOIN_PHASE_INSTANCE_COUNT, VKD3DSIH_DCL_HS_JOIN_PHASE_INSTANCE_COUNT, "",     "",
            shader_sm4_read_declaration_count},
    {VKD3D_SM5_OP_DCL_THREAD_GROUP,                 VKD3DSIH_DCL_THREAD_GROUP,                 "",     "",
            shader_sm5_read_dcl_thread_group},
    {VKD3D_SM5_OP_DCL_UAV_TYPED,                    VKD3DSIH_DCL_UAV_TYPED,                    "",     "",
            shader_sm4_read_dcl_resource},
    {VKD3D_SM5_OP_DCL_UAV_RAW,                      VKD3DSIH_DCL_UAV_RAW,                      "",     "",
            shader_sm5_read_dcl_uav_raw},
    {VKD3D_SM5_OP_DCL_UAV_STRUCTURED,               VKD3DSIH_DCL_UAV_STRUCTURED,               "",     "",
            shader_sm5_read_dcl_uav_structured},
    {VKD3D_SM5_OP_DCL_TGSM_RAW,                     VKD3DSIH_DCL_TGSM_RAW,                     "",     "",
            shader_sm5_read_dcl_tgsm_raw},
    {VKD3D_SM5_OP_DCL_TGSM_STRUCTURED,              VKD3DSIH_DCL_TGSM_STRUCTURED,              "",     "",
            shader_sm5_read_dcl_tgsm_structured},
    {VKD3D_SM5_OP_DCL_RESOURCE_RAW,                 VKD3DSIH_DCL_RESOURCE_RAW,                 "",     "",
            shader_sm5_read_dcl_resource_raw},
    {VKD3D_SM5_OP_DCL_RESOURCE_STRUCTURED,          VKD3DSIH_DCL_RESOURCE_STRUCTURED,          "",     "",
            shader_sm5_read_dcl_resource_structured},
    {VKD3D_SM5_OP_LD_UAV_TYPED,                     VKD3DSIH_LD_UAV_TYPED,                     "u",    "iU"},
    {VKD3D_SM5_OP_STORE_UAV_TYPED,                  VKD3DSIH_STORE_UAV_TYPED,                  "U",    "iu"},
    {VKD3D_SM5_OP_LD_RAW,                           VKD3DSIH_LD_RAW,                           "u",    "iU"},
    {VKD3D_SM5_OP_STORE_RAW,                        VKD3DSIH_STORE_RAW,                        "U",    "uu"},
    {VKD3D_SM5_OP_LD_STRUCTURED,                    VKD3DSIH_LD_STRUCTURED,                    "u",    "iiR"},
    {VKD3D_SM5_OP_STORE_STRUCTURED,                 VKD3DSIH_STORE_STRUCTURED,                 "U",    "iiu"},
    {VKD3D_SM5_OP_ATOMIC_AND,                       VKD3DSIH_ATOMIC_AND,                       "U",    "iu"},
    {VKD3D_SM5_OP_ATOMIC_OR,                        VKD3DSIH_ATOMIC_OR,                        "U",    "iu"},
    {VKD3D_SM5_OP_ATOMIC_XOR,                       VKD3DSIH_ATOMIC_XOR,                       "U",    "iu"},
    {VKD3D_SM5_OP_ATOMIC_CMP_STORE,                 VKD3DSIH_ATOMIC_CMP_STORE,                 "U",    "iuu"},
    {VKD3D_SM5_OP_ATOMIC_IADD,                      VKD3DSIH_ATOMIC_IADD,                      "U",    "ii"},
    {VKD3D_SM5_OP_ATOMIC_IMAX,                      VKD3DSIH_ATOMIC_IMAX,                      "U",    "ii"},
    {VKD3D_SM5_OP_ATOMIC_IMIN,                      VKD3DSIH_ATOMIC_IMIN,                      "U",    "ii"},
    {VKD3D_SM5_OP_ATOMIC_UMAX,                      VKD3DSIH_ATOMIC_UMAX,                      "U",    "iu"},
    {VKD3D_SM5_OP_ATOMIC_UMIN,                      VKD3DSIH_ATOMIC_UMIN,                      "U",    "iu"},
    {VKD3D_SM5_OP_IMM_ATOMIC_ALLOC,                 VKD3DSIH_IMM_ATOMIC_ALLOC,                 "u",    "U"},
    {VKD3D_SM5_OP_IMM_ATOMIC_CONSUME,               VKD3DSIH_IMM_ATOMIC_CONSUME,               "u",    "U"},
    {VKD3D_SM5_OP_IMM_ATOMIC_IADD,                  VKD3DSIH_IMM_ATOMIC_IADD,                  "uU",   "ii"},
    {VKD3D_SM5_OP_IMM_ATOMIC_AND,                   VKD3DSIH_IMM_ATOMIC_AND,                   "uU",   "iu"},
    {VKD3D_SM5_OP_IMM_ATOMIC_OR,                    VKD3DSIH_IMM_ATOMIC_OR,                    "uU",   "iu"},
    {VKD3D_SM5_OP_IMM_ATOMIC_XOR,                   VKD3DSIH_IMM_ATOMIC_XOR,                   "uU",   "iu"},
    {VKD3D_SM5_OP_IMM_ATOMIC_EXCH,                  VKD3DSIH_IMM_ATOMIC_EXCH,                  "uU",   "iu"},
    {VKD3D_SM5_OP_IMM_ATOMIC_CMP_EXCH,              VKD3DSIH_IMM_ATOMIC_CMP_EXCH,              "uU",   "iuu"},
    {VKD3D_SM5_OP_IMM_ATOMIC_IMAX,                  VKD3DSIH_IMM_ATOMIC_IMAX,                  "iU",   "ii"},
    {VKD3D_SM5_OP_IMM_ATOMIC_IMIN,                  VKD3DSIH_IMM_ATOMIC_IMIN,                  "iU",   "ii"},
    {VKD3D_SM5_OP_IMM_ATOMIC_UMAX,                  VKD3DSIH_IMM_ATOMIC_UMAX,                  "uU",   "iu"},
    {VKD3D_SM5_OP_IMM_ATOMIC_UMIN,                  VKD3DSIH_IMM_ATOMIC_UMIN,                  "uU",   "iu"},
    {VKD3D_SM5_OP_SYNC,                             VKD3DSIH_SYNC,                             "",     "",
            shader_sm5_read_sync},
    {VKD3D_SM5_OP_DADD,                             VKD3DSIH_DADD,                             "d",    "dd"},
    {VKD3D_SM5_OP_DMAX,                             VKD3DSIH_DMAX,                             "d",    "dd"},
    {VKD3D_SM5_OP_DMIN,                             VKD3DSIH_DMIN,                             "d",    "dd"},
    {VKD3D_SM5_OP_DMUL,                             VKD3DSIH_DMUL,                             "d",    "dd"},
    {VKD3D_SM5_OP_DEQ,                              VKD3DSIH_DEQ,                              "u",    "dd"},
    {VKD3D_SM5_OP_DGE,                              VKD3DSIH_DGE,                              "u",    "dd"},
    {VKD3D_SM5_OP_DLT,                              VKD3DSIH_DLT,                              "u",    "dd"},
    {VKD3D_SM5_OP_DNE,                              VKD3DSIH_DNE,                              "u",    "dd"},
    {VKD3D_SM5_OP_DMOV,                             VKD3DSIH_DMOV,                             "d",    "d"},
    {VKD3D_SM5_OP_DMOVC,                            VKD3DSIH_DMOVC,                            "d",    "udd"},
    {VKD3D_SM5_OP_DTOF,                             VKD3DSIH_DTOF,                             "f",    "d"},
    {VKD3D_SM5_OP_FTOD,                             VKD3DSIH_FTOD,                             "d",    "f"},
    {VKD3D_SM5_OP_EVAL_SAMPLE_INDEX,                VKD3DSIH_EVAL_SAMPLE_INDEX,                "f",    "fi"},
    {VKD3D_SM5_OP_EVAL_CENTROID,                    VKD3DSIH_EVAL_CENTROID,                    "f",    "f"},
    {VKD3D_SM5_OP_DCL_GS_INSTANCES,                 VKD3DSIH_DCL_GS_INSTANCES,                 "",     "",
            shader_sm4_read_declaration_count},
    {VKD3D_SM5_OP_DDIV,                             VKD3DSIH_DDIV,                             "d",    "dd"},
    {VKD3D_SM5_OP_DFMA,                             VKD3DSIH_DFMA,                             "d",    "ddd"},
    {VKD3D_SM5_OP_DRCP,                             VKD3DSIH_DRCP,                             "d",    "d"},
    {VKD3D_SM5_OP_MSAD,                             VKD3DSIH_MSAD,                             "u",    "uuu"},
    {VKD3D_SM5_OP_DTOI,                             VKD3DSIH_DTOI,                             "i",    "d"},
    {VKD3D_SM5_OP_DTOU,                             VKD3DSIH_DTOU,                             "u",    "d"},
    {VKD3D_SM5_OP_ITOD,                             VKD3DSIH_ITOD,                             "d",    "i"},
    {VKD3D_SM5_OP_UTOD,                             VKD3DSIH_UTOD,                             "d",    "u"},
    {VKD3D_SM5_OP_GATHER4_S,                        VKD3DSIH_GATHER4_S,                        "uu",   "fRS"},
    {VKD3D_SM5_OP_GATHER4_C_S,                      VKD3DSIH_GATHER4_C_S,                      "fu",   "fRSf"},
    {VKD3D_SM5_OP_GATHER4_PO_S,                     VKD3DSIH_GATHER4_PO_S,                     "fu",   "fiRS"},
    {VKD3D_SM5_OP_GATHER4_PO_C_S,                   VKD3DSIH_GATHER4_PO_C_S,                   "fu",   "fiRSf"},
    {VKD3D_SM5_OP_LD_S,                             VKD3DSIH_LD_S,                             "uu",   "iR"},
    {VKD3D_SM5_OP_LD2DMS_S,                         VKD3DSIH_LD2DMS_S,                         "uu",   "iRi"},
    {VKD3D_SM5_OP_LD_UAV_TYPED_S,                   VKD3DSIH_LD_UAV_TYPED_S,                   "uu",   "iU"},
    {VKD3D_SM5_OP_LD_RAW_S,                         VKD3DSIH_LD_RAW_S,                         "uu",   "iU"},
    {VKD3D_SM5_OP_LD_STRUCTURED_S,                  VKD3DSIH_LD_STRUCTURED_S,                  "uu",   "iiR"},
    {VKD3D_SM5_OP_SAMPLE_LOD_S,                     VKD3DSIH_SAMPLE_LOD_S,                     "uu",   "fRSf"},
    {VKD3D_SM5_OP_SAMPLE_C_LZ_S,                    VKD3DSIH_SAMPLE_C_LZ_S,                    "fu",   "fRSf"},
    {VKD3D_SM5_OP_SAMPLE_CL_S,                      VKD3DSIH_SAMPLE_CL_S,                      "uu",   "fRSf"},
    {VKD3D_SM5_OP_SAMPLE_B_CL_S,                    VKD3DSIH_SAMPLE_B_CL_S,                    "uu",   "fRSff"},
    {VKD3D_SM5_OP_SAMPLE_GRAD_CL_S,                 VKD3DSIH_SAMPLE_GRAD_CL_S,                 "uu",   "fRSfff"},
    {VKD3D_SM5_OP_SAMPLE_C_CL_S,                    VKD3DSIH_SAMPLE_C_CL_S,                    "fu",   "fRSff"},
    {VKD3D_SM5_OP_CHECK_ACCESS_FULLY_MAPPED,        VKD3DSIH_CHECK_ACCESS_FULLY_MAPPED,        "u",    "u"},
};

static const enum vkd3d_shader_register_type register_type_table[] =
{
    /* VKD3D_SM4_RT_TEMP */                    VKD3DSPR_TEMP,
    /* VKD3D_SM4_RT_INPUT */                   VKD3DSPR_INPUT,
    /* VKD3D_SM4_RT_OUTPUT */                  VKD3DSPR_OUTPUT,
    /* VKD3D_SM4_RT_INDEXABLE_TEMP */          VKD3DSPR_IDXTEMP,
    /* VKD3D_SM4_RT_IMMCONST */                VKD3DSPR_IMMCONST,
    /* VKD3D_SM4_RT_IMMCONST64 */              VKD3DSPR_IMMCONST64,
    /* VKD3D_SM4_RT_SAMPLER */                 VKD3DSPR_SAMPLER,
    /* VKD3D_SM4_RT_RESOURCE */                VKD3DSPR_RESOURCE,
    /* VKD3D_SM4_RT_CONSTBUFFER */             VKD3DSPR_CONSTBUFFER,
    /* VKD3D_SM4_RT_IMMCONSTBUFFER */          VKD3DSPR_IMMCONSTBUFFER,
    /* UNKNOWN */                              ~0u,
    /* VKD3D_SM4_RT_PRIMID */                  VKD3DSPR_PRIMID,
    /* VKD3D_SM4_RT_DEPTHOUT */                VKD3DSPR_DEPTHOUT,
    /* VKD3D_SM4_RT_NULL */                    VKD3DSPR_NULL,
    /* VKD3D_SM4_RT_RASTERIZER */              VKD3DSPR_RASTERIZER,
    /* VKD3D_SM4_RT_OMASK */                   VKD3DSPR_SAMPLEMASK,
    /* VKD3D_SM5_RT_STREAM */                  VKD3DSPR_STREAM,
    /* VKD3D_SM5_RT_FUNCTION_BODY */           VKD3DSPR_FUNCTIONBODY,
    /* UNKNOWN */                              ~0u,
    /* VKD3D_SM5_RT_FUNCTION_POINTER */        VKD3DSPR_FUNCTIONPOINTER,
    /* UNKNOWN */                              ~0u,
    /* UNKNOWN */                              ~0u,
    /* VKD3D_SM5_RT_OUTPUT_CONTROL_POINT_ID */ VKD3DSPR_OUTPOINTID,
    /* VKD3D_SM5_RT_FORK_INSTANCE_ID */        VKD3DSPR_FORKINSTID,
    /* VKD3D_SM5_RT_JOIN_INSTANCE_ID */        VKD3DSPR_JOININSTID,
    /* VKD3D_SM5_RT_INPUT_CONTROL_POINT */     VKD3DSPR_INCONTROLPOINT,
    /* VKD3D_SM5_RT_OUTPUT_CONTROL_POINT */    VKD3DSPR_OUTCONTROLPOINT,
    /* VKD3D_SM5_RT_PATCH_CONSTANT_DATA */     VKD3DSPR_PATCHCONST,
    /* VKD3D_SM5_RT_DOMAIN_LOCATION */         VKD3DSPR_TESSCOORD,
    /* UNKNOWN */                              ~0u,
    /* VKD3D_SM5_RT_UAV */                     VKD3DSPR_UAV,
    /* VKD3D_SM5_RT_SHARED_MEMORY */           VKD3DSPR_GROUPSHAREDMEM,
    /* VKD3D_SM5_RT_THREAD_ID */               VKD3DSPR_THREADID,
    /* VKD3D_SM5_RT_THREAD_GROUP_ID */         VKD3DSPR_THREADGROUPID,
    /* VKD3D_SM5_RT_LOCAL_THREAD_ID */         VKD3DSPR_LOCALTHREADID,
    /* VKD3D_SM5_RT_COVERAGE */                VKD3DSPR_COVERAGE,
    /* VKD3D_SM5_RT_LOCAL_THREAD_INDEX */      VKD3DSPR_LOCALTHREADINDEX,
    /* VKD3D_SM5_RT_GS_INSTANCE_ID */          VKD3DSPR_GSINSTID,
    /* VKD3D_SM5_RT_DEPTHOUT_GREATER_EQUAL */  VKD3DSPR_DEPTHOUTGE,
    /* VKD3D_SM5_RT_DEPTHOUT_LESS_EQUAL */     VKD3DSPR_DEPTHOUTLE,
    /* VKD3D_SM5_RT_CYCLE_COUNTER */           ~0u,
    /* VKD3D_SM5_RT_OUTPUT_STENCIL_REF */      VKD3DSPR_OUTSTENCILREF,
};

static const enum vkd3d_shader_register_precision register_precision_table[] =
{
    /* VKD3D_SM4_REGISTER_PRECISION_DEFAULT */      VKD3D_SHADER_REGISTER_PRECISION_DEFAULT,
    /* VKD3D_SM4_REGISTER_PRECISION_MIN_FLOAT_16 */ VKD3D_SHADER_REGISTER_PRECISION_MIN_FLOAT_16,
    /* VKD3D_SM4_REGISTER_PRECISION_MIN_FLOAT_10 */ VKD3D_SHADER_REGISTER_PRECISION_MIN_FLOAT_10,
    /* UNKNOWN */                                   VKD3D_SHADER_REGISTER_PRECISION_INVALID,
    /* VKD3D_SM4_REGISTER_PRECISION_MIN_INT_16 */   VKD3D_SHADER_REGISTER_PRECISION_MIN_INT_16,
    /* VKD3D_SM4_REGISTER_PRECISION_MIN_UINT_16 */  VKD3D_SHADER_REGISTER_PRECISION_MIN_UINT_16,
};

static const struct vkd3d_sm4_opcode_info *get_opcode_info(enum vkd3d_sm4_opcode opcode)
{
    unsigned int i;

    for (i = 0; i < sizeof(opcode_table) / sizeof(*opcode_table); ++i)
    {
        if (opcode == opcode_table[i].opcode) return &opcode_table[i];
    }

    return NULL;
}

static void map_register(const struct vkd3d_shader_sm4_parser *sm4, struct vkd3d_shader_register *reg)
{
    switch (sm4->p.shader_version.type)
    {
        case VKD3D_SHADER_TYPE_PIXEL:
            if (reg->type == VKD3DSPR_OUTPUT)
            {
                unsigned int reg_idx = reg->idx[0].offset;

                if (reg_idx >= ARRAY_SIZE(sm4->output_map))
                {
                    ERR("Invalid output index %u.\n", reg_idx);
                    break;
                }

                reg->type = VKD3DSPR_COLOROUT;
                reg->idx[0].offset = sm4->output_map[reg_idx];
            }
            break;

        default:
            break;
    }
}

static enum vkd3d_data_type map_data_type(char t)
{
    switch (t)
    {
        case 'd':
            return VKD3D_DATA_DOUBLE;
        case 'f':
            return VKD3D_DATA_FLOAT;
        case 'i':
            return VKD3D_DATA_INT;
        case 'u':
            return VKD3D_DATA_UINT;
        case 'O':
            return VKD3D_DATA_OPAQUE;
        case 'R':
            return VKD3D_DATA_RESOURCE;
        case 'S':
            return VKD3D_DATA_SAMPLER;
        case 'U':
            return VKD3D_DATA_UAV;
        default:
            ERR("Invalid data type '%c'.\n", t);
            return VKD3D_DATA_FLOAT;
    }
}

static void shader_sm4_destroy(struct vkd3d_shader_parser *parser)
{
    struct vkd3d_shader_sm4_parser *sm4 = vkd3d_shader_sm4_parser(parser);

    shader_instruction_array_destroy(&parser->instructions);
    free_shader_desc(&parser->shader_desc);
    vkd3d_free(sm4);
}

static bool shader_sm4_read_reg_idx(struct vkd3d_shader_sm4_parser *priv, const uint32_t **ptr,
        const uint32_t *end, uint32_t addressing, struct vkd3d_shader_register_index *reg_idx)
{
    if (addressing & VKD3D_SM4_ADDRESSING_RELATIVE)
    {
        struct vkd3d_shader_src_param *rel_addr = shader_parser_get_src_params(&priv->p, 1);

        if (!(reg_idx->rel_addr = rel_addr))
        {
            ERR("Failed to get src param for relative addressing.\n");
            return false;
        }

        if (addressing & VKD3D_SM4_ADDRESSING_OFFSET)
            reg_idx->offset = *(*ptr)++;
        else
            reg_idx->offset = 0;
        shader_sm4_read_src_param(priv, ptr, end, VKD3D_DATA_INT, rel_addr);
    }
    else
    {
        reg_idx->rel_addr = NULL;
        reg_idx->offset = *(*ptr)++;
    }

    return true;
}

static bool sm4_register_is_descriptor(enum vkd3d_sm4_register_type register_type)
{
    switch (register_type)
    {
        case VKD3D_SM4_RT_SAMPLER:
        case VKD3D_SM4_RT_RESOURCE:
        case VKD3D_SM4_RT_CONSTBUFFER:
        case VKD3D_SM5_RT_UAV:
            return true;

        default:
            return false;
    }
}

static bool shader_sm4_read_param(struct vkd3d_shader_sm4_parser *priv, const uint32_t **ptr, const uint32_t *end,
        enum vkd3d_data_type data_type, struct vkd3d_shader_register *param, enum vkd3d_shader_src_modifier *modifier)
{
    enum vkd3d_sm4_register_precision precision;
    enum vkd3d_sm4_register_type register_type;
    enum vkd3d_sm4_extended_operand_type type;
    enum vkd3d_sm4_register_modifier m;
    uint32_t token, order, extended;

    if (*ptr >= end)
    {
        WARN("Invalid ptr %p >= end %p.\n", *ptr, end);
        return false;
    }
    token = *(*ptr)++;

    register_type = (token & VKD3D_SM4_REGISTER_TYPE_MASK) >> VKD3D_SM4_REGISTER_TYPE_SHIFT;
    if (register_type >= ARRAY_SIZE(register_type_table)
            || register_type_table[register_type] == VKD3DSPR_INVALID)
    {
        FIXME("Unhandled register type %#x.\n", register_type);
        param->type = VKD3DSPR_TEMP;
    }
    else
    {
        param->type = register_type_table[register_type];
    }
    param->precision = VKD3D_SHADER_REGISTER_PRECISION_DEFAULT;
    param->non_uniform = false;
    param->data_type = data_type;

    *modifier = VKD3DSPSM_NONE;
    if (token & VKD3D_SM4_EXTENDED_OPERAND)
    {
        if (*ptr >= end)
        {
            WARN("Invalid ptr %p >= end %p.\n", *ptr, end);
            return false;
        }
        extended = *(*ptr)++;

        if (extended & VKD3D_SM4_EXTENDED_OPERAND)
        {
            FIXME("Skipping second-order extended operand.\n");
            *ptr += *ptr < end;
        }

        type = extended & VKD3D_SM4_EXTENDED_OPERAND_TYPE_MASK;
        if (type == VKD3D_SM4_EXTENDED_OPERAND_MODIFIER)
        {
            m = (extended & VKD3D_SM4_REGISTER_MODIFIER_MASK) >> VKD3D_SM4_REGISTER_MODIFIER_SHIFT;
            switch (m)
            {
                case VKD3D_SM4_REGISTER_MODIFIER_NEGATE:
                    *modifier = VKD3DSPSM_NEG;
                    break;

                case VKD3D_SM4_REGISTER_MODIFIER_ABS:
                    *modifier = VKD3DSPSM_ABS;
                    break;

                case VKD3D_SM4_REGISTER_MODIFIER_ABS_NEGATE:
                    *modifier = VKD3DSPSM_ABSNEG;
                    break;

                default:
                    FIXME("Unhandled register modifier %#x.\n", m);
                    /* fall-through */
                case VKD3D_SM4_REGISTER_MODIFIER_NONE:
                    break;
            }

            precision = (extended & VKD3D_SM4_REGISTER_PRECISION_MASK) >> VKD3D_SM4_REGISTER_PRECISION_SHIFT;
            if (precision >= ARRAY_SIZE(register_precision_table)
                    || register_precision_table[precision] == VKD3D_SHADER_REGISTER_PRECISION_INVALID)
            {
                FIXME("Unhandled register precision %#x.\n", precision);
                param->precision = VKD3D_SHADER_REGISTER_PRECISION_INVALID;
            }
            else
            {
                param->precision = register_precision_table[precision];
            }

            if (extended & VKD3D_SM4_REGISTER_NON_UNIFORM_MASK)
                param->non_uniform = true;

            extended &= ~(VKD3D_SM4_EXTENDED_OPERAND_TYPE_MASK | VKD3D_SM4_REGISTER_MODIFIER_MASK
                    | VKD3D_SM4_REGISTER_PRECISION_MASK | VKD3D_SM4_REGISTER_NON_UNIFORM_MASK
                    | VKD3D_SM4_EXTENDED_OPERAND);
            if (extended)
                FIXME("Skipping unhandled extended operand bits 0x%08x.\n", extended);
        }
        else if (type)
        {
            FIXME("Skipping unhandled extended operand token 0x%08x (type %#x).\n", extended, type);
        }
    }

    order = (token & VKD3D_SM4_REGISTER_ORDER_MASK) >> VKD3D_SM4_REGISTER_ORDER_SHIFT;

    if (order < 1)
    {
        param->idx[0].offset = ~0u;
        param->idx[0].rel_addr = NULL;
    }
    else
    {
        DWORD addressing = (token & VKD3D_SM4_ADDRESSING_MASK0) >> VKD3D_SM4_ADDRESSING_SHIFT0;
        if (!(shader_sm4_read_reg_idx(priv, ptr, end, addressing, &param->idx[0])))
        {
            ERR("Failed to read register index.\n");
            return false;
        }
    }

    if (order < 2)
    {
        param->idx[1].offset = ~0u;
        param->idx[1].rel_addr = NULL;
    }
    else
    {
        DWORD addressing = (token & VKD3D_SM4_ADDRESSING_MASK1) >> VKD3D_SM4_ADDRESSING_SHIFT1;
        if (!(shader_sm4_read_reg_idx(priv, ptr, end, addressing, &param->idx[1])))
        {
            ERR("Failed to read register index.\n");
            return false;
        }
    }

    if (order < 3)
    {
        param->idx[2].offset = ~0u;
        param->idx[2].rel_addr = NULL;
    }
    else
    {
        DWORD addressing = (token & VKD3D_SM4_ADDRESSING_MASK2) >> VKD3D_SM4_ADDRESSING_SHIFT2;
        if (!(shader_sm4_read_reg_idx(priv, ptr, end, addressing, &param->idx[2])))
        {
            ERR("Failed to read register index.\n");
            return false;
        }
    }

    if (order > 3)
    {
        WARN("Unhandled order %u.\n", order);
        return false;
    }

    if (register_type == VKD3D_SM4_RT_IMMCONST || register_type == VKD3D_SM4_RT_IMMCONST64)
    {
        enum vkd3d_sm4_dimension dimension = (token & VKD3D_SM4_DIMENSION_MASK) >> VKD3D_SM4_DIMENSION_SHIFT;
        unsigned int dword_count;

        switch (dimension)
        {
            case VKD3D_SM4_DIMENSION_SCALAR:
                param->immconst_type = VKD3D_IMMCONST_SCALAR;
                dword_count = 1 + (register_type == VKD3D_SM4_RT_IMMCONST64);
                if (end - *ptr < dword_count)
                {
                    WARN("Invalid ptr %p, end %p.\n", *ptr, end);
                    return false;
                }
                memcpy(param->u.immconst_uint, *ptr, dword_count * sizeof(DWORD));
                *ptr += dword_count;
                break;

            case VKD3D_SM4_DIMENSION_VEC4:
                param->immconst_type = VKD3D_IMMCONST_VEC4;
                if (end - *ptr < VKD3D_VEC4_SIZE)
                {
                    WARN("Invalid ptr %p, end %p.\n", *ptr, end);
                    return false;
                }
                memcpy(param->u.immconst_uint, *ptr, VKD3D_VEC4_SIZE * sizeof(DWORD));
                *ptr += 4;
                break;

            default:
                FIXME("Unhandled dimension %#x.\n", dimension);
                break;
        }
    }
    else if (!shader_is_sm_5_1(priv) && sm4_register_is_descriptor(register_type))
    {
        /* SM5.1 places a symbol identifier in idx[0] and moves
         * other values up one slot. Normalize to SM5.1. */
        param->idx[2] = param->idx[1];
        param->idx[1] = param->idx[0];
    }

    map_register(priv, param);

    return true;
}

static bool shader_sm4_is_scalar_register(const struct vkd3d_shader_register *reg)
{
    switch (reg->type)
    {
        case VKD3DSPR_COVERAGE:
        case VKD3DSPR_DEPTHOUT:
        case VKD3DSPR_DEPTHOUTGE:
        case VKD3DSPR_DEPTHOUTLE:
        case VKD3DSPR_GSINSTID:
        case VKD3DSPR_LOCALTHREADINDEX:
        case VKD3DSPR_OUTPOINTID:
        case VKD3DSPR_PRIMID:
        case VKD3DSPR_SAMPLEMASK:
        case VKD3DSPR_OUTSTENCILREF:
            return true;
        default:
            return false;
    }
}

static uint32_t swizzle_from_sm4(uint32_t s)
{
    return vkd3d_shader_create_swizzle(s & 0x3, (s >> 2) & 0x3, (s >> 4) & 0x3, (s >> 6) & 0x3);
}

static bool shader_sm4_read_src_param(struct vkd3d_shader_sm4_parser *priv, const uint32_t **ptr,
        const uint32_t *end, enum vkd3d_data_type data_type, struct vkd3d_shader_src_param *src_param)
{
    DWORD token;

    if (*ptr >= end)
    {
        WARN("Invalid ptr %p >= end %p.\n", *ptr, end);
        return false;
    }
    token = **ptr;

    if (!shader_sm4_read_param(priv, ptr, end, data_type, &src_param->reg, &src_param->modifiers))
    {
        ERR("Failed to read parameter.\n");
        return false;
    }

    if (src_param->reg.type == VKD3DSPR_IMMCONST || src_param->reg.type == VKD3DSPR_IMMCONST64)
    {
        src_param->swizzle = VKD3D_SHADER_NO_SWIZZLE;
    }
    else
    {
        enum vkd3d_sm4_swizzle_type swizzle_type =
                (token & VKD3D_SM4_SWIZZLE_TYPE_MASK) >> VKD3D_SM4_SWIZZLE_TYPE_SHIFT;

        switch (swizzle_type)
        {
            case VKD3D_SM4_SWIZZLE_NONE:
                if (shader_sm4_is_scalar_register(&src_param->reg))
                    src_param->swizzle = VKD3D_SHADER_SWIZZLE(X, X, X, X);
                else
                    src_param->swizzle = VKD3D_SHADER_NO_SWIZZLE;
                break;

            case VKD3D_SM4_SWIZZLE_SCALAR:
                src_param->swizzle = (token & VKD3D_SM4_SWIZZLE_MASK) >> VKD3D_SM4_SWIZZLE_SHIFT;
                src_param->swizzle = (src_param->swizzle & 0x3) * 0x01010101;
                break;

            case VKD3D_SM4_SWIZZLE_VEC4:
                src_param->swizzle = swizzle_from_sm4((token & VKD3D_SM4_SWIZZLE_MASK) >> VKD3D_SM4_SWIZZLE_SHIFT);
                break;

            default:
                FIXME("Unhandled swizzle type %#x.\n", swizzle_type);
                break;
        }
    }

    return true;
}

static bool shader_sm4_read_dst_param(struct vkd3d_shader_sm4_parser *priv, const uint32_t **ptr,
        const uint32_t *end, enum vkd3d_data_type data_type, struct vkd3d_shader_dst_param *dst_param)
{
    enum vkd3d_shader_src_modifier modifier;
    DWORD token;

    if (*ptr >= end)
    {
        WARN("Invalid ptr %p >= end %p.\n", *ptr, end);
        return false;
    }
    token = **ptr;

    if (!shader_sm4_read_param(priv, ptr, end, data_type, &dst_param->reg, &modifier))
    {
        ERR("Failed to read parameter.\n");
        return false;
    }

    if (modifier != VKD3DSPSM_NONE)
    {
        ERR("Invalid source modifier %#x on destination register.\n", modifier);
        return false;
    }

    dst_param->write_mask = (token & VKD3D_SM4_WRITEMASK_MASK) >> VKD3D_SM4_WRITEMASK_SHIFT;
    if (data_type == VKD3D_DATA_DOUBLE)
        dst_param->write_mask = vkd3d_write_mask_64_from_32(dst_param->write_mask);
    /* Scalar registers are declared with no write mask in shader bytecode. */
    if (!dst_param->write_mask && shader_sm4_is_scalar_register(&dst_param->reg))
        dst_param->write_mask = VKD3DSP_WRITEMASK_0;
    dst_param->modifiers = 0;
    dst_param->shift = 0;

    return true;
}

static void shader_sm4_read_instruction_modifier(DWORD modifier, struct vkd3d_shader_instruction *ins)
{
    enum vkd3d_sm4_instruction_modifier modifier_type = modifier & VKD3D_SM4_MODIFIER_MASK;

    switch (modifier_type)
    {
        case VKD3D_SM4_MODIFIER_AOFFIMMI:
        {
            static const DWORD recognized_bits = VKD3D_SM4_INSTRUCTION_MODIFIER
                    | VKD3D_SM4_MODIFIER_MASK
                    | VKD3D_SM4_AOFFIMMI_U_MASK
                    | VKD3D_SM4_AOFFIMMI_V_MASK
                    | VKD3D_SM4_AOFFIMMI_W_MASK;

            /* Bit fields are used for sign extension. */
            struct
            {
                int u : 4;
                int v : 4;
                int w : 4;
            } aoffimmi;

            if (modifier & ~recognized_bits)
                FIXME("Unhandled instruction modifier %#x.\n", modifier);

            aoffimmi.u = (modifier & VKD3D_SM4_AOFFIMMI_U_MASK) >> VKD3D_SM4_AOFFIMMI_U_SHIFT;
            aoffimmi.v = (modifier & VKD3D_SM4_AOFFIMMI_V_MASK) >> VKD3D_SM4_AOFFIMMI_V_SHIFT;
            aoffimmi.w = (modifier & VKD3D_SM4_AOFFIMMI_W_MASK) >> VKD3D_SM4_AOFFIMMI_W_SHIFT;
            ins->texel_offset.u = aoffimmi.u;
            ins->texel_offset.v = aoffimmi.v;
            ins->texel_offset.w = aoffimmi.w;
            break;
        }

        case VKD3D_SM5_MODIFIER_DATA_TYPE:
        {
            DWORD components = (modifier & VKD3D_SM5_MODIFIER_DATA_TYPE_MASK) >> VKD3D_SM5_MODIFIER_DATA_TYPE_SHIFT;
            unsigned int i;

            for (i = 0; i < VKD3D_VEC4_SIZE; i++)
            {
                enum vkd3d_sm4_data_type data_type = VKD3D_SM4_TYPE_COMPONENT(components, i);

                if (!data_type || (data_type >= ARRAY_SIZE(data_type_table)))
                {
                    FIXME("Unhandled data type %#x.\n", data_type);
                    ins->resource_data_type[i] = VKD3D_DATA_FLOAT;
                }
                else
                {
                    ins->resource_data_type[i] = data_type_table[data_type];
                }
            }
            break;
        }

        case VKD3D_SM5_MODIFIER_RESOURCE_TYPE:
        {
            enum vkd3d_sm4_resource_type resource_type
                    = (modifier & VKD3D_SM5_MODIFIER_RESOURCE_TYPE_MASK) >> VKD3D_SM5_MODIFIER_RESOURCE_TYPE_SHIFT;

            if (resource_type == VKD3D_SM4_RESOURCE_RAW_BUFFER)
                ins->raw = true;
            else if (resource_type == VKD3D_SM4_RESOURCE_STRUCTURED_BUFFER)
                ins->structured = true;

            if (resource_type < ARRAY_SIZE(resource_type_table))
                ins->resource_type = resource_type_table[resource_type];
            else
            {
                FIXME("Unhandled resource type %#x.\n", resource_type);
                ins->resource_type = VKD3D_SHADER_RESOURCE_NONE;
            }

            ins->resource_stride
                    = (modifier & VKD3D_SM5_MODIFIER_RESOURCE_STRIDE_MASK) >> VKD3D_SM5_MODIFIER_RESOURCE_STRIDE_SHIFT;
            break;
        }

        default:
            FIXME("Unhandled instruction modifier %#x.\n", modifier);
    }
}

static void shader_sm4_read_instruction(struct vkd3d_shader_sm4_parser *sm4, struct vkd3d_shader_instruction *ins)
{
    const struct vkd3d_sm4_opcode_info *opcode_info;
    uint32_t opcode_token, opcode, previous_token;
    struct vkd3d_shader_dst_param *dst_params;
    struct vkd3d_shader_src_param *src_params;
    const uint32_t **ptr = &sm4->ptr;
    unsigned int i, len;
    size_t remaining;
    const uint32_t *p;
    DWORD precise;

    if (*ptr >= sm4->end)
    {
        WARN("End of byte-code, failed to read opcode.\n");
        goto fail;
    }
    remaining = sm4->end - *ptr;

    ++sm4->p.location.line;

    opcode_token = *(*ptr)++;
    opcode = opcode_token & VKD3D_SM4_OPCODE_MASK;

    len = ((opcode_token & VKD3D_SM4_INSTRUCTION_LENGTH_MASK) >> VKD3D_SM4_INSTRUCTION_LENGTH_SHIFT);
    if (!len)
    {
        if (remaining < 2)
        {
            WARN("End of byte-code, failed to read length token.\n");
            goto fail;
        }
        len = **ptr;
    }
    if (!len || remaining < len)
    {
        WARN("Read invalid length %u (remaining %zu).\n", len, remaining);
        goto fail;
    }
    --len;

    if (!(opcode_info = get_opcode_info(opcode)))
    {
        FIXME("Unrecognized opcode %#x, opcode_token 0x%08x.\n", opcode, opcode_token);
        ins->handler_idx = VKD3DSIH_INVALID;
        *ptr += len;
        return;
    }

    ins->handler_idx = opcode_info->handler_idx;
    ins->flags = 0;
    ins->coissue = false;
    ins->raw = false;
    ins->structured = false;
    ins->predicate = NULL;
    ins->dst_count = strnlen(opcode_info->dst_info, SM4_MAX_DST_COUNT);
    ins->src_count = strnlen(opcode_info->src_info, SM4_MAX_SRC_COUNT);
    ins->src = src_params = shader_parser_get_src_params(&sm4->p, ins->src_count);
    if (!src_params && ins->src_count)
    {
        ERR("Failed to allocate src parameters.\n");
        vkd3d_shader_parser_error(&sm4->p, VKD3D_SHADER_ERROR_TPF_OUT_OF_MEMORY, "Out of memory.");
        ins->handler_idx = VKD3DSIH_INVALID;
        return;
    }
    ins->resource_type = VKD3D_SHADER_RESOURCE_NONE;
    ins->resource_stride = 0;
    ins->resource_data_type[0] = VKD3D_DATA_FLOAT;
    ins->resource_data_type[1] = VKD3D_DATA_FLOAT;
    ins->resource_data_type[2] = VKD3D_DATA_FLOAT;
    ins->resource_data_type[3] = VKD3D_DATA_FLOAT;
    memset(&ins->texel_offset, 0, sizeof(ins->texel_offset));

    p = *ptr;
    *ptr += len;

    if (opcode_info->read_opcode_func)
    {
        ins->dst = NULL;
        ins->dst_count = 0;
        opcode_info->read_opcode_func(ins, opcode, opcode_token, p, len, sm4);
    }
    else
    {
        enum vkd3d_shader_dst_modifier instruction_dst_modifier = VKD3DSPDM_NONE;

        previous_token = opcode_token;
        while (previous_token & VKD3D_SM4_INSTRUCTION_MODIFIER && p != *ptr)
            shader_sm4_read_instruction_modifier(previous_token = *p++, ins);

        ins->flags = (opcode_token & VKD3D_SM4_INSTRUCTION_FLAGS_MASK) >> VKD3D_SM4_INSTRUCTION_FLAGS_SHIFT;
        if (ins->flags & VKD3D_SM4_INSTRUCTION_FLAG_SATURATE)
        {
            ins->flags &= ~VKD3D_SM4_INSTRUCTION_FLAG_SATURATE;
            instruction_dst_modifier = VKD3DSPDM_SATURATE;
        }
        precise = (opcode_token & VKD3D_SM5_PRECISE_MASK) >> VKD3D_SM5_PRECISE_SHIFT;
        ins->flags |= precise << VKD3DSI_PRECISE_SHIFT;

        ins->dst = dst_params = shader_parser_get_dst_params(&sm4->p, ins->dst_count);
        if (!dst_params && ins->dst_count)
        {
            ERR("Failed to allocate dst parameters.\n");
            vkd3d_shader_parser_error(&sm4->p, VKD3D_SHADER_ERROR_TPF_OUT_OF_MEMORY, "Out of memory.");
            ins->handler_idx = VKD3DSIH_INVALID;
            return;
        }
        for (i = 0; i < ins->dst_count; ++i)
        {
            if (!(shader_sm4_read_dst_param(sm4, &p, *ptr, map_data_type(opcode_info->dst_info[i]),
                    &dst_params[i])))
            {
                ins->handler_idx = VKD3DSIH_INVALID;
                return;
            }
            dst_params[i].modifiers |= instruction_dst_modifier;
        }

        for (i = 0; i < ins->src_count; ++i)
        {
            if (!(shader_sm4_read_src_param(sm4, &p, *ptr, map_data_type(opcode_info->src_info[i]),
                    &src_params[i])))
            {
                ins->handler_idx = VKD3DSIH_INVALID;
                return;
            }
        }
    }

    return;

fail:
    *ptr = sm4->end;
    ins->handler_idx = VKD3DSIH_INVALID;
    return;
}

static const struct vkd3d_shader_parser_ops shader_sm4_parser_ops =
{
    .parser_destroy = shader_sm4_destroy,
};

static bool shader_sm4_init(struct vkd3d_shader_sm4_parser *sm4, const uint32_t *byte_code,
        size_t byte_code_size, const char *source_name, const struct shader_signature *output_signature,
        struct vkd3d_shader_message_context *message_context)
{
    struct vkd3d_shader_version version;
    uint32_t version_token, token_count;
    unsigned int i;

    if (byte_code_size / sizeof(*byte_code) < 2)
    {
        WARN("Invalid byte code size %lu.\n", (long)byte_code_size);
        return false;
    }

    version_token = byte_code[0];
    TRACE("Version: 0x%08x.\n", version_token);
    token_count = byte_code[1];
    TRACE("Token count: %u.\n", token_count);

    if (token_count < 2 || byte_code_size / sizeof(*byte_code) < token_count)
    {
        WARN("Invalid token count %u.\n", token_count);
        return false;
    }

    sm4->start = &byte_code[2];
    sm4->end = &byte_code[token_count];

    switch (version_token >> 16)
    {
        case VKD3D_SM4_PS:
            version.type = VKD3D_SHADER_TYPE_PIXEL;
            break;

        case VKD3D_SM4_VS:
            version.type = VKD3D_SHADER_TYPE_VERTEX;
            break;

        case VKD3D_SM4_GS:
            version.type = VKD3D_SHADER_TYPE_GEOMETRY;
            break;

        case VKD3D_SM5_HS:
            version.type = VKD3D_SHADER_TYPE_HULL;
            break;

        case VKD3D_SM5_DS:
            version.type = VKD3D_SHADER_TYPE_DOMAIN;
            break;

        case VKD3D_SM5_CS:
            version.type = VKD3D_SHADER_TYPE_COMPUTE;
            break;

        default:
            FIXME("Unrecognised shader type %#x.\n", version_token >> 16);
    }
    version.major = VKD3D_SM4_VERSION_MAJOR(version_token);
    version.minor = VKD3D_SM4_VERSION_MINOR(version_token);

    /* Estimate instruction count to avoid reallocation in most shaders. */
    if (!vkd3d_shader_parser_init(&sm4->p, message_context, source_name, &version, &shader_sm4_parser_ops,
            token_count / 7u + 20))
        return false;
    sm4->ptr = sm4->start;

    memset(sm4->output_map, 0xff, sizeof(sm4->output_map));
    for (i = 0; i < output_signature->element_count; ++i)
    {
        struct signature_element *e = &output_signature->elements[i];

        if (version.type == VKD3D_SHADER_TYPE_PIXEL
                && ascii_strcasecmp(e->semantic_name, "SV_Target"))
            continue;
        if (e->register_index >= ARRAY_SIZE(sm4->output_map))
        {
            WARN("Invalid output index %u.\n", e->register_index);
            continue;
        }

        sm4->output_map[e->register_index] = e->semantic_index;
    }

    return true;
}

int vkd3d_shader_sm4_parser_create(const struct vkd3d_shader_compile_info *compile_info,
        struct vkd3d_shader_message_context *message_context, struct vkd3d_shader_parser **parser)
{
    struct vkd3d_shader_instruction_array *instructions;
    struct vkd3d_shader_desc *shader_desc;
    struct vkd3d_shader_instruction *ins;
    struct vkd3d_shader_sm4_parser *sm4;
    int ret;

    if (!(sm4 = vkd3d_calloc(1, sizeof(*sm4))))
    {
        ERR("Failed to allocate parser.\n");
        return VKD3D_ERROR_OUT_OF_MEMORY;
    }

    shader_desc = &sm4->p.shader_desc;
    if ((ret = shader_extract_from_dxbc(&compile_info->source,
            message_context, compile_info->source_name, shader_desc)) < 0)
    {
        WARN("Failed to extract shader, vkd3d result %d.\n", ret);
        vkd3d_free(sm4);
        return ret;
    }

    if (!shader_sm4_init(sm4, shader_desc->byte_code, shader_desc->byte_code_size,
            compile_info->source_name, &shader_desc->output_signature, message_context))
    {
        WARN("Failed to initialise shader parser.\n");
        free_shader_desc(shader_desc);
        vkd3d_free(sm4);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    instructions = &sm4->p.instructions;
    while (sm4->ptr != sm4->end)
    {
        if (!shader_instruction_array_reserve(instructions, instructions->count + 1))
        {
            ERR("Failed to allocate instructions.\n");
            vkd3d_shader_parser_error(&sm4->p, VKD3D_SHADER_ERROR_TPF_OUT_OF_MEMORY, "Out of memory.");
            shader_sm4_destroy(&sm4->p);
            return VKD3D_ERROR_OUT_OF_MEMORY;
        }
        ins = &instructions->elements[instructions->count];
        shader_sm4_read_instruction(sm4, ins);

        if (ins->handler_idx == VKD3DSIH_INVALID)
        {
            WARN("Encountered unrecognized or invalid instruction.\n");
            shader_sm4_destroy(&sm4->p);
            return VKD3D_ERROR_OUT_OF_MEMORY;
        }
        ++instructions->count;
    }

    *parser = &sm4->p;

    return sm4->p.failed ? VKD3D_ERROR_INVALID_SHADER : VKD3D_OK;
}

static void write_sm4_block(struct hlsl_ctx *ctx, struct vkd3d_bytecode_buffer *buffer, const struct hlsl_block *block);

static bool type_is_integer(const struct hlsl_type *type)
{
    switch (type->base_type)
    {
        case HLSL_TYPE_BOOL:
        case HLSL_TYPE_INT:
        case HLSL_TYPE_UINT:
            return true;

        default:
            return false;
    }
}

bool hlsl_sm4_register_from_semantic(struct hlsl_ctx *ctx, const struct hlsl_semantic *semantic,
        bool output, unsigned int *type, enum vkd3d_sm4_swizzle_type *swizzle_type, bool *has_idx)
{
    unsigned int i;

    static const struct
    {
        const char *semantic;
        bool output;
        enum vkd3d_shader_type shader_type;
        enum vkd3d_sm4_swizzle_type swizzle_type;
        enum vkd3d_sm4_register_type type;
        bool has_idx;
    }
    register_table[] =
    {
        {"sv_dispatchthreadid", false, VKD3D_SHADER_TYPE_COMPUTE,   VKD3D_SM4_SWIZZLE_VEC4, VKD3D_SM5_RT_THREAD_ID,         false},
        {"sv_groupid",          false, VKD3D_SHADER_TYPE_COMPUTE,   VKD3D_SM4_SWIZZLE_VEC4, VKD3D_SM5_RT_THREAD_GROUP_ID,   false},
        {"sv_groupthreadid",    false, VKD3D_SHADER_TYPE_COMPUTE,   VKD3D_SM4_SWIZZLE_VEC4, VKD3D_SM5_RT_LOCAL_THREAD_ID,   false},

        {"sv_primitiveid",      false, VKD3D_SHADER_TYPE_GEOMETRY,  VKD3D_SM4_SWIZZLE_NONE, VKD3D_SM4_RT_PRIMID,            false},

        /* Put sv_target in this table, instead of letting it fall through to
         * default varying allocation, so that the register index matches the
         * usage index. */
        {"color",               true,  VKD3D_SHADER_TYPE_PIXEL,     VKD3D_SM4_SWIZZLE_VEC4, VKD3D_SM4_RT_OUTPUT,            true},
        {"depth",               true,  VKD3D_SHADER_TYPE_PIXEL,     VKD3D_SM4_SWIZZLE_VEC4, VKD3D_SM4_RT_DEPTHOUT,          false},
        {"sv_depth",            true,  VKD3D_SHADER_TYPE_PIXEL,     VKD3D_SM4_SWIZZLE_VEC4, VKD3D_SM4_RT_DEPTHOUT,          false},
        {"sv_target",           true,  VKD3D_SHADER_TYPE_PIXEL,     VKD3D_SM4_SWIZZLE_VEC4, VKD3D_SM4_RT_OUTPUT,            true},
    };

    for (i = 0; i < ARRAY_SIZE(register_table); ++i)
    {
        if (!ascii_strcasecmp(semantic->name, register_table[i].semantic)
                && output == register_table[i].output
                && ctx->profile->type == register_table[i].shader_type)
        {
            *type = register_table[i].type;
            if (swizzle_type)
                *swizzle_type = register_table[i].swizzle_type;
            *has_idx = register_table[i].has_idx;
            return true;
        }
    }

    return false;
}

bool hlsl_sm4_usage_from_semantic(struct hlsl_ctx *ctx, const struct hlsl_semantic *semantic,
        bool output, D3D_NAME *usage)
{
    unsigned int i;

    static const struct
    {
        const char *name;
        bool output;
        enum vkd3d_shader_type shader_type;
        D3DDECLUSAGE usage;
    }
    semantics[] =
    {
        {"sv_dispatchthreadid",         false, VKD3D_SHADER_TYPE_COMPUTE,   ~0u},
        {"sv_groupid",                  false, VKD3D_SHADER_TYPE_COMPUTE,   ~0u},
        {"sv_groupthreadid",            false, VKD3D_SHADER_TYPE_COMPUTE,   ~0u},

        {"position",                    false, VKD3D_SHADER_TYPE_GEOMETRY,  D3D_NAME_POSITION},
        {"sv_position",                 false, VKD3D_SHADER_TYPE_GEOMETRY,  D3D_NAME_POSITION},
        {"sv_primitiveid",              false, VKD3D_SHADER_TYPE_GEOMETRY,  D3D_NAME_PRIMITIVE_ID},

        {"position",                    true,  VKD3D_SHADER_TYPE_GEOMETRY,  D3D_NAME_POSITION},
        {"sv_position",                 true,  VKD3D_SHADER_TYPE_GEOMETRY,  D3D_NAME_POSITION},
        {"sv_primitiveid",              true,  VKD3D_SHADER_TYPE_GEOMETRY,  D3D_NAME_PRIMITIVE_ID},

        {"position",                    false, VKD3D_SHADER_TYPE_PIXEL,     D3D_NAME_POSITION},
        {"sv_position",                 false, VKD3D_SHADER_TYPE_PIXEL,     D3D_NAME_POSITION},
        {"sv_isfrontface",              false, VKD3D_SHADER_TYPE_PIXEL,     D3D_NAME_IS_FRONT_FACE},

        {"color",                       true,  VKD3D_SHADER_TYPE_PIXEL,     D3D_NAME_TARGET},
        {"depth",                       true,  VKD3D_SHADER_TYPE_PIXEL,     D3D_NAME_DEPTH},
        {"sv_target",                   true,  VKD3D_SHADER_TYPE_PIXEL,     D3D_NAME_TARGET},
        {"sv_depth",                    true,  VKD3D_SHADER_TYPE_PIXEL,     D3D_NAME_DEPTH},

        {"sv_position",                 false, VKD3D_SHADER_TYPE_VERTEX,    D3D_NAME_UNDEFINED},
        {"sv_vertexid",                 false, VKD3D_SHADER_TYPE_VERTEX,    D3D_NAME_VERTEX_ID},

        {"position",                    true,  VKD3D_SHADER_TYPE_VERTEX,    D3D_NAME_POSITION},
        {"sv_position",                 true,  VKD3D_SHADER_TYPE_VERTEX,    D3D_NAME_POSITION},
    };

    for (i = 0; i < ARRAY_SIZE(semantics); ++i)
    {
        if (!ascii_strcasecmp(semantic->name, semantics[i].name)
                && output == semantics[i].output
                && ctx->profile->type == semantics[i].shader_type
                && !ascii_strncasecmp(semantic->name, "sv_", 3))
        {
            *usage = semantics[i].usage;
            return true;
        }
    }

    if (!ascii_strncasecmp(semantic->name, "sv_", 3))
        return false;

    *usage = D3D_NAME_UNDEFINED;
    return true;
}

static void add_section(struct dxbc_writer *dxbc, uint32_t tag, struct vkd3d_bytecode_buffer *buffer)
{
    /* Native D3DDisassemble() expects at least the sizes of the ISGN and OSGN
     * sections to be aligned. Without this, the sections themselves will be
     * aligned, but their reported sizes won't. */
    size_t size = bytecode_align(buffer);

    dxbc_writer_add_section(dxbc, tag, buffer->data, size);
}

static void write_sm4_signature(struct hlsl_ctx *ctx, struct dxbc_writer *dxbc, bool output)
{
    struct vkd3d_bytecode_buffer buffer = {0};
    struct vkd3d_string_buffer *string;
    const struct hlsl_ir_var *var;
    size_t count_position;
    unsigned int i;
    bool ret;

    count_position = put_u32(&buffer, 0);
    put_u32(&buffer, 8); /* unknown */

    LIST_FOR_EACH_ENTRY(var, &ctx->extern_vars, struct hlsl_ir_var, extern_entry)
    {
        unsigned int width = (1u << var->data_type->dimx) - 1, use_mask;
        enum vkd3d_sm4_register_type type;
        uint32_t usage_idx, reg_idx;
        D3D_NAME usage;
        bool has_idx;

        if ((output && !var->is_output_semantic) || (!output && !var->is_input_semantic))
            continue;

        ret = hlsl_sm4_usage_from_semantic(ctx, &var->semantic, output, &usage);
        assert(ret);
        if (usage == ~0u)
            continue;
        usage_idx = var->semantic.index;

        if (hlsl_sm4_register_from_semantic(ctx, &var->semantic, output, &type, NULL, &has_idx))
        {
            reg_idx = has_idx ? var->semantic.index : ~0u;
        }
        else
        {
            assert(var->regs[HLSL_REGSET_NUMERIC].allocated);
            type = VKD3D_SM4_RT_INPUT;
            reg_idx = var->regs[HLSL_REGSET_NUMERIC].id;
        }

        use_mask = width; /* FIXME: accurately report use mask */
        if (output)
            use_mask = 0xf ^ use_mask;

        /* Special pixel shader semantics (TARGET, DEPTH, COVERAGE). */
        if (usage >= 64)
            usage = 0;

        put_u32(&buffer, 0); /* name */
        put_u32(&buffer, usage_idx);
        put_u32(&buffer, usage);
        switch (var->data_type->base_type)
        {
            case HLSL_TYPE_FLOAT:
            case HLSL_TYPE_HALF:
                put_u32(&buffer, D3D_REGISTER_COMPONENT_FLOAT32);
                break;

            case HLSL_TYPE_INT:
                put_u32(&buffer, D3D_REGISTER_COMPONENT_SINT32);
                break;

            case HLSL_TYPE_BOOL:
            case HLSL_TYPE_UINT:
                put_u32(&buffer, D3D_REGISTER_COMPONENT_UINT32);
                break;

            default:
                if ((string = hlsl_type_to_string(ctx, var->data_type)))
                    hlsl_error(ctx, &var->loc, VKD3D_SHADER_ERROR_HLSL_INVALID_TYPE,
                            "Invalid data type %s for semantic variable %s.", string->buffer, var->name);
                hlsl_release_string_buffer(ctx, string);
                put_u32(&buffer, D3D_REGISTER_COMPONENT_UNKNOWN);
        }
        put_u32(&buffer, reg_idx);
        put_u32(&buffer, vkd3d_make_u16(width, use_mask));
    }

    i = 0;
    LIST_FOR_EACH_ENTRY(var, &ctx->extern_vars, struct hlsl_ir_var, extern_entry)
    {
        const char *semantic = var->semantic.name;
        size_t string_offset;
        D3D_NAME usage;

        if ((output && !var->is_output_semantic) || (!output && !var->is_input_semantic))
            continue;

        hlsl_sm4_usage_from_semantic(ctx, &var->semantic, output, &usage);
        if (usage == ~0u)
            continue;

        if (usage == D3D_NAME_TARGET && !ascii_strcasecmp(semantic, "color"))
            string_offset = put_string(&buffer, "SV_Target");
        else if (usage == D3D_NAME_DEPTH && !ascii_strcasecmp(semantic, "depth"))
            string_offset = put_string(&buffer, "SV_Depth");
        else if (usage == D3D_NAME_POSITION && !ascii_strcasecmp(semantic, "position"))
            string_offset = put_string(&buffer, "SV_Position");
        else
            string_offset = put_string(&buffer, semantic);
        set_u32(&buffer, (2 + i++ * 6) * sizeof(uint32_t), string_offset);
    }

    set_u32(&buffer, count_position, i);

    add_section(dxbc, output ? TAG_OSGN : TAG_ISGN, &buffer);
}

static D3D_SHADER_VARIABLE_CLASS sm4_class(const struct hlsl_type *type)
{
    switch (type->class)
    {
        case HLSL_CLASS_ARRAY:
            return sm4_class(type->e.array.type);
        case HLSL_CLASS_MATRIX:
            assert(type->modifiers & HLSL_MODIFIERS_MAJORITY_MASK);
            if (type->modifiers & HLSL_MODIFIER_COLUMN_MAJOR)
                return D3D_SVC_MATRIX_COLUMNS;
            else
                return D3D_SVC_MATRIX_ROWS;
        case HLSL_CLASS_OBJECT:
            return D3D_SVC_OBJECT;
        case HLSL_CLASS_SCALAR:
            return D3D_SVC_SCALAR;
        case HLSL_CLASS_STRUCT:
            return D3D_SVC_STRUCT;
        case HLSL_CLASS_VECTOR:
            return D3D_SVC_VECTOR;
        default:
            ERR("Invalid class %#x.\n", type->class);
            vkd3d_unreachable();
    }
}

static D3D_SHADER_VARIABLE_TYPE sm4_base_type(const struct hlsl_type *type)
{
    switch (type->base_type)
    {
        case HLSL_TYPE_BOOL:
            return D3D_SVT_BOOL;
        case HLSL_TYPE_DOUBLE:
            return D3D_SVT_DOUBLE;
        case HLSL_TYPE_FLOAT:
        case HLSL_TYPE_HALF:
            return D3D_SVT_FLOAT;
        case HLSL_TYPE_INT:
            return D3D_SVT_INT;
        case HLSL_TYPE_PIXELSHADER:
            return D3D_SVT_PIXELSHADER;
        case HLSL_TYPE_SAMPLER:
            switch (type->sampler_dim)
            {
                case HLSL_SAMPLER_DIM_1D:
                    return D3D_SVT_SAMPLER1D;
                case HLSL_SAMPLER_DIM_2D:
                    return D3D_SVT_SAMPLER2D;
                case HLSL_SAMPLER_DIM_3D:
                    return D3D_SVT_SAMPLER3D;
                case HLSL_SAMPLER_DIM_CUBE:
                    return D3D_SVT_SAMPLERCUBE;
                case HLSL_SAMPLER_DIM_GENERIC:
                    return D3D_SVT_SAMPLER;
                default:
                    vkd3d_unreachable();
            }
            break;
        case HLSL_TYPE_STRING:
            return D3D_SVT_STRING;
        case HLSL_TYPE_TEXTURE:
            switch (type->sampler_dim)
            {
                case HLSL_SAMPLER_DIM_1D:
                    return D3D_SVT_TEXTURE1D;
                case HLSL_SAMPLER_DIM_2D:
                    return D3D_SVT_TEXTURE2D;
                case HLSL_SAMPLER_DIM_3D:
                    return D3D_SVT_TEXTURE3D;
                case HLSL_SAMPLER_DIM_CUBE:
                    return D3D_SVT_TEXTURECUBE;
                case HLSL_SAMPLER_DIM_GENERIC:
                    return D3D_SVT_TEXTURE;
                default:
                    vkd3d_unreachable();
            }
            break;
        case HLSL_TYPE_UINT:
            return D3D_SVT_UINT;
        case HLSL_TYPE_VERTEXSHADER:
            return D3D_SVT_VERTEXSHADER;
        case HLSL_TYPE_VOID:
            return D3D_SVT_VOID;
        default:
            vkd3d_unreachable();
    }
}

static void write_sm4_type(struct hlsl_ctx *ctx, struct vkd3d_bytecode_buffer *buffer, struct hlsl_type *type)
{
    const struct hlsl_type *array_type = hlsl_get_multiarray_element_type(type);
    const char *name = array_type->name ? array_type->name : "<unnamed>";
    const struct hlsl_profile_info *profile = ctx->profile;
    unsigned int field_count = 0, array_size = 0;
    size_t fields_offset = 0, name_offset = 0;
    size_t i;

    if (type->bytecode_offset)
        return;

    if (profile->major_version >= 5)
        name_offset = put_string(buffer, name);

    if (type->class == HLSL_CLASS_ARRAY)
        array_size = hlsl_get_multiarray_size(type);

    if (array_type->class == HLSL_CLASS_STRUCT)
    {
        field_count = array_type->e.record.field_count;

        for (i = 0; i < field_count; ++i)
        {
            struct hlsl_struct_field *field = &array_type->e.record.fields[i];

            field->name_bytecode_offset = put_string(buffer, field->name);
            write_sm4_type(ctx, buffer, field->type);
        }

        fields_offset = bytecode_align(buffer);

        for (i = 0; i < field_count; ++i)
        {
            struct hlsl_struct_field *field = &array_type->e.record.fields[i];

            put_u32(buffer, field->name_bytecode_offset);
            put_u32(buffer, field->type->bytecode_offset);
            put_u32(buffer, field->reg_offset[HLSL_REGSET_NUMERIC]);
        }
    }

    type->bytecode_offset = put_u32(buffer, vkd3d_make_u32(sm4_class(type), sm4_base_type(type)));
    put_u32(buffer, vkd3d_make_u32(type->dimy, type->dimx));
    put_u32(buffer, vkd3d_make_u32(array_size, field_count));
    put_u32(buffer, fields_offset);

    if (profile->major_version >= 5)
    {
        put_u32(buffer, 0); /* FIXME: unknown */
        put_u32(buffer, 0); /* FIXME: unknown */
        put_u32(buffer, 0); /* FIXME: unknown */
        put_u32(buffer, 0); /* FIXME: unknown */
        put_u32(buffer, name_offset);
    }
}

static D3D_SHADER_INPUT_TYPE sm4_resource_type(const struct hlsl_type *type)
{
    if (type->class == HLSL_CLASS_ARRAY)
        return sm4_resource_type(type->e.array.type);

    switch (type->base_type)
    {
        case HLSL_TYPE_SAMPLER:
            return D3D_SIT_SAMPLER;
        case HLSL_TYPE_TEXTURE:
            return D3D_SIT_TEXTURE;
        case HLSL_TYPE_UAV:
            return D3D_SIT_UAV_RWTYPED;
        default:
            vkd3d_unreachable();
    }
}

static D3D_RESOURCE_RETURN_TYPE sm4_resource_format(const struct hlsl_type *type)
{
    if (type->class == HLSL_CLASS_ARRAY)
        return sm4_resource_format(type->e.array.type);

    switch (type->e.resource_format->base_type)
    {
        case HLSL_TYPE_DOUBLE:
            return D3D_RETURN_TYPE_DOUBLE;

        case HLSL_TYPE_FLOAT:
        case HLSL_TYPE_HALF:
            return D3D_RETURN_TYPE_FLOAT;

        case HLSL_TYPE_INT:
            return D3D_RETURN_TYPE_SINT;
            break;

        case HLSL_TYPE_BOOL:
        case HLSL_TYPE_UINT:
            return D3D_RETURN_TYPE_UINT;

        default:
            vkd3d_unreachable();
    }
}

static D3D_SRV_DIMENSION sm4_rdef_resource_dimension(const struct hlsl_type *type)
{
    if (type->class == HLSL_CLASS_ARRAY)
        return sm4_rdef_resource_dimension(type->e.array.type);

    switch (type->sampler_dim)
    {
        case HLSL_SAMPLER_DIM_1D:
            return D3D_SRV_DIMENSION_TEXTURE1D;
        case HLSL_SAMPLER_DIM_2D:
            return D3D_SRV_DIMENSION_TEXTURE2D;
        case HLSL_SAMPLER_DIM_3D:
            return D3D_SRV_DIMENSION_TEXTURE3D;
        case HLSL_SAMPLER_DIM_CUBE:
            return D3D_SRV_DIMENSION_TEXTURECUBE;
        case HLSL_SAMPLER_DIM_1DARRAY:
            return D3D_SRV_DIMENSION_TEXTURE1DARRAY;
        case HLSL_SAMPLER_DIM_2DARRAY:
            return D3D_SRV_DIMENSION_TEXTURE2DARRAY;
        case HLSL_SAMPLER_DIM_2DMS:
            return D3D_SRV_DIMENSION_TEXTURE2DMS;
        case HLSL_SAMPLER_DIM_2DMSARRAY:
            return D3D_SRV_DIMENSION_TEXTURE2DMSARRAY;
        case HLSL_SAMPLER_DIM_CUBEARRAY:
            return D3D_SRV_DIMENSION_TEXTURECUBEARRAY;
        default:
            vkd3d_unreachable();
    }
}

static int sm4_compare_extern_resources(const void *a, const void *b)
{
    const struct hlsl_ir_var *aa = *(const struct hlsl_ir_var **)a;
    const struct hlsl_ir_var *bb = *(const struct hlsl_ir_var **)b;
    enum hlsl_regset aa_regset, bb_regset;

    aa_regset = hlsl_type_get_regset(aa->data_type);
    bb_regset = hlsl_type_get_regset(bb->data_type);

    if (aa_regset != bb_regset)
        return aa_regset - bb_regset;

    return aa->regs[aa_regset].id - bb->regs[bb_regset].id;
}

static const struct hlsl_ir_var **sm4_get_extern_resources(struct hlsl_ctx *ctx, unsigned int *count)
{
    const struct hlsl_ir_var **extern_resources = NULL;
    const struct hlsl_ir_var *var;
    enum hlsl_regset regset;
    size_t capacity = 0;

    *count = 0;

    LIST_FOR_EACH_ENTRY(var, &ctx->extern_vars, struct hlsl_ir_var, extern_entry)
    {
        if (!hlsl_type_is_resource(var->data_type))
            continue;
        regset = hlsl_type_get_regset(var->data_type);
        if (!var->regs[regset].allocated)
            continue;

        if (!(hlsl_array_reserve(ctx, (void **)&extern_resources, &capacity, *count + 1,
                sizeof(*extern_resources))))
        {
            *count = 0;
            return NULL;
        }

        extern_resources[*count] = var;
        ++*count;
    }

    qsort(extern_resources, *count, sizeof(*extern_resources), sm4_compare_extern_resources);
    return extern_resources;
}

static void write_sm4_rdef(struct hlsl_ctx *ctx, struct dxbc_writer *dxbc)
{
    unsigned int cbuffer_count = 0, resource_count = 0, extern_resources_count, i, j;
    size_t cbuffers_offset, resources_offset, creator_offset, string_offset;
    size_t cbuffer_position, resource_position, creator_position;
    const struct hlsl_profile_info *profile = ctx->profile;
    const struct hlsl_ir_var **extern_resources;
    struct vkd3d_bytecode_buffer buffer = {0};
    const struct hlsl_buffer *cbuffer;
    const struct hlsl_ir_var *var;

    static const uint16_t target_types[] =
    {
        0xffff, /* PIXEL */
        0xfffe, /* VERTEX */
        0x4753, /* GEOMETRY */
        0x4853, /* HULL */
        0x4453, /* DOMAIN */
        0x4353, /* COMPUTE */
    };

    extern_resources = sm4_get_extern_resources(ctx, &extern_resources_count);

    resource_count += extern_resources_count;
    LIST_FOR_EACH_ENTRY(cbuffer, &ctx->buffers, struct hlsl_buffer, entry)
    {
        if (cbuffer->reg.allocated)
        {
            ++cbuffer_count;
            ++resource_count;
        }
    }

    put_u32(&buffer, cbuffer_count);
    cbuffer_position = put_u32(&buffer, 0);
    put_u32(&buffer, resource_count);
    resource_position = put_u32(&buffer, 0);
    put_u32(&buffer, vkd3d_make_u32(vkd3d_make_u16(profile->minor_version, profile->major_version),
            target_types[profile->type]));
    put_u32(&buffer, 0); /* FIXME: compilation flags */
    creator_position = put_u32(&buffer, 0);

    if (profile->major_version >= 5)
    {
        put_u32(&buffer, TAG_RD11);
        put_u32(&buffer, 15 * sizeof(uint32_t)); /* size of RDEF header including this header */
        put_u32(&buffer, 6 * sizeof(uint32_t)); /* size of buffer desc */
        put_u32(&buffer, 8 * sizeof(uint32_t)); /* size of binding desc */
        put_u32(&buffer, 10 * sizeof(uint32_t)); /* size of variable desc */
        put_u32(&buffer, 9 * sizeof(uint32_t)); /* size of type desc */
        put_u32(&buffer, 3 * sizeof(uint32_t)); /* size of member desc */
        put_u32(&buffer, 0); /* unknown; possibly a null terminator */
    }

    /* Bound resources. */

    resources_offset = bytecode_align(&buffer);
    set_u32(&buffer, resource_position, resources_offset);

    for (i = 0; i < extern_resources_count; ++i)
    {
        enum hlsl_regset regset;
        uint32_t flags = 0;

        var = extern_resources[i];
        regset = hlsl_type_get_regset(var->data_type);

        if (var->reg_reservation.reg_type)
            flags |= D3D_SIF_USERPACKED;

        put_u32(&buffer, 0); /* name */
        put_u32(&buffer, sm4_resource_type(var->data_type));
        if (regset == HLSL_REGSET_SAMPLERS)
        {
            put_u32(&buffer, 0);
            put_u32(&buffer, 0);
            put_u32(&buffer, 0);
        }
        else
        {
            unsigned int dimx = hlsl_type_get_component_type(ctx, var->data_type, 0)->e.resource_format->dimx;

            put_u32(&buffer, sm4_resource_format(var->data_type));
            put_u32(&buffer, sm4_rdef_resource_dimension(var->data_type));
            put_u32(&buffer, ~0u); /* FIXME: multisample count */
            flags |= (dimx - 1) << VKD3D_SM4_SIF_TEXTURE_COMPONENTS_SHIFT;
        }
        put_u32(&buffer, var->regs[regset].id);
        put_u32(&buffer, var->regs[regset].bind_count);
        put_u32(&buffer, flags);
    }

    LIST_FOR_EACH_ENTRY(cbuffer, &ctx->buffers, struct hlsl_buffer, entry)
    {
        uint32_t flags = 0;

        if (!cbuffer->reg.allocated)
            continue;

        if (cbuffer->reservation.reg_type)
            flags |= D3D_SIF_USERPACKED;

        put_u32(&buffer, 0); /* name */
        put_u32(&buffer, cbuffer->type == HLSL_BUFFER_CONSTANT ? D3D_SIT_CBUFFER : D3D_SIT_TBUFFER);
        put_u32(&buffer, 0); /* return type */
        put_u32(&buffer, 0); /* dimension */
        put_u32(&buffer, 0); /* multisample count */
        put_u32(&buffer, cbuffer->reg.id); /* bind point */
        put_u32(&buffer, 1); /* bind count */
        put_u32(&buffer, flags); /* flags */
    }

    for (i = 0; i < extern_resources_count; ++i)
    {
        var = extern_resources[i];

        string_offset = put_string(&buffer, var->name);
        set_u32(&buffer, resources_offset + i * 8 * sizeof(uint32_t), string_offset);
    }

    LIST_FOR_EACH_ENTRY(cbuffer, &ctx->buffers, struct hlsl_buffer, entry)
    {
        if (!cbuffer->reg.allocated)
            continue;

        string_offset = put_string(&buffer, cbuffer->name);
        set_u32(&buffer, resources_offset + i++ * 8 * sizeof(uint32_t), string_offset);
    }

    /* Buffers. */

    cbuffers_offset = bytecode_align(&buffer);
    set_u32(&buffer, cbuffer_position, cbuffers_offset);
    LIST_FOR_EACH_ENTRY(cbuffer, &ctx->buffers, struct hlsl_buffer, entry)
    {
        unsigned int var_count = 0;

        if (!cbuffer->reg.allocated)
            continue;

        LIST_FOR_EACH_ENTRY(var, &ctx->extern_vars, struct hlsl_ir_var, extern_entry)
        {
            if (var->is_uniform && var->buffer == cbuffer)
                ++var_count;
        }

        put_u32(&buffer, 0); /* name */
        put_u32(&buffer, var_count);
        put_u32(&buffer, 0); /* variable offset */
        put_u32(&buffer, align(cbuffer->size, 4) * sizeof(float));
        put_u32(&buffer, 0); /* FIXME: flags */
        put_u32(&buffer, cbuffer->type == HLSL_BUFFER_CONSTANT ? D3D_CT_CBUFFER : D3D_CT_TBUFFER);
    }

    i = 0;
    LIST_FOR_EACH_ENTRY(cbuffer, &ctx->buffers, struct hlsl_buffer, entry)
    {
        if (!cbuffer->reg.allocated)
            continue;

        string_offset = put_string(&buffer, cbuffer->name);
        set_u32(&buffer, cbuffers_offset + i++ * 6 * sizeof(uint32_t), string_offset);
    }

    i = 0;
    LIST_FOR_EACH_ENTRY(cbuffer, &ctx->buffers, struct hlsl_buffer, entry)
    {
        size_t vars_start = bytecode_align(&buffer);

        if (!cbuffer->reg.allocated)
            continue;

        set_u32(&buffer, cbuffers_offset + (i++ * 6 + 2) * sizeof(uint32_t), vars_start);

        LIST_FOR_EACH_ENTRY(var, &ctx->extern_vars, struct hlsl_ir_var, extern_entry)
        {
            if (var->is_uniform && var->buffer == cbuffer)
            {
                uint32_t flags = 0;

                if (var->last_read)
                    flags |= D3D_SVF_USED;

                put_u32(&buffer, 0); /* name */
                put_u32(&buffer, var->buffer_offset * sizeof(float));
                put_u32(&buffer, var->data_type->reg_size[HLSL_REGSET_NUMERIC] * sizeof(float));
                put_u32(&buffer, flags);
                put_u32(&buffer, 0); /* type */
                put_u32(&buffer, 0); /* FIXME: default value */

                if (profile->major_version >= 5)
                {
                    put_u32(&buffer, 0); /* texture start */
                    put_u32(&buffer, 0); /* texture count */
                    put_u32(&buffer, 0); /* sampler start */
                    put_u32(&buffer, 0); /* sampler count */
                }
            }
        }

        j = 0;
        LIST_FOR_EACH_ENTRY(var, &ctx->extern_vars, struct hlsl_ir_var, extern_entry)
        {
            if (var->is_uniform && var->buffer == cbuffer)
            {
                const unsigned int var_size = (profile->major_version >= 5 ? 10 : 6);
                size_t var_offset = vars_start + j * var_size * sizeof(uint32_t);
                size_t string_offset = put_string(&buffer, var->name);

                set_u32(&buffer, var_offset, string_offset);
                write_sm4_type(ctx, &buffer, var->data_type);
                set_u32(&buffer, var_offset + 4 * sizeof(uint32_t), var->data_type->bytecode_offset);
                ++j;
            }
        }
    }

    creator_offset = put_string(&buffer, vkd3d_shader_get_version(NULL, NULL));
    set_u32(&buffer, creator_position, creator_offset);

    add_section(dxbc, TAG_RDEF, &buffer);

    vkd3d_free(extern_resources);
}

static enum vkd3d_sm4_resource_type sm4_resource_dimension(const struct hlsl_type *type)
{
    switch (type->sampler_dim)
    {
        case HLSL_SAMPLER_DIM_1D:
            return VKD3D_SM4_RESOURCE_TEXTURE_1D;
        case HLSL_SAMPLER_DIM_2D:
            return VKD3D_SM4_RESOURCE_TEXTURE_2D;
        case HLSL_SAMPLER_DIM_3D:
            return VKD3D_SM4_RESOURCE_TEXTURE_3D;
        case HLSL_SAMPLER_DIM_CUBE:
            return VKD3D_SM4_RESOURCE_TEXTURE_CUBE;
        case HLSL_SAMPLER_DIM_1DARRAY:
            return VKD3D_SM4_RESOURCE_TEXTURE_1DARRAY;
        case HLSL_SAMPLER_DIM_2DARRAY:
            return VKD3D_SM4_RESOURCE_TEXTURE_2DARRAY;
        case HLSL_SAMPLER_DIM_2DMS:
            return VKD3D_SM4_RESOURCE_TEXTURE_2DMS;
        case HLSL_SAMPLER_DIM_2DMSARRAY:
            return VKD3D_SM4_RESOURCE_TEXTURE_2DMSARRAY;
        case HLSL_SAMPLER_DIM_CUBEARRAY:
            return VKD3D_SM4_RESOURCE_TEXTURE_CUBEARRAY;
        default:
            vkd3d_unreachable();
    }
}

struct sm4_instruction_modifier
{
    enum vkd3d_sm4_instruction_modifier type;

    union
    {
        struct
        {
            int u, v, w;
        } aoffimmi;
    } u;
};

static uint32_t sm4_encode_instruction_modifier(const struct sm4_instruction_modifier *imod)
{
    uint32_t word = 0;

    word |= VKD3D_SM4_MODIFIER_MASK & imod->type;

    switch (imod->type)
    {
        case VKD3D_SM4_MODIFIER_AOFFIMMI:
            assert(-8 <= imod->u.aoffimmi.u && imod->u.aoffimmi.u <= 7);
            assert(-8 <= imod->u.aoffimmi.v && imod->u.aoffimmi.v <= 7);
            assert(-8 <= imod->u.aoffimmi.w && imod->u.aoffimmi.w <= 7);
            word |= ((uint32_t)imod->u.aoffimmi.u & 0xf) << VKD3D_SM4_AOFFIMMI_U_SHIFT;
            word |= ((uint32_t)imod->u.aoffimmi.v & 0xf) << VKD3D_SM4_AOFFIMMI_V_SHIFT;
            word |= ((uint32_t)imod->u.aoffimmi.w & 0xf) << VKD3D_SM4_AOFFIMMI_W_SHIFT;
            break;

        default:
            vkd3d_unreachable();
    }

    return word;
}

struct sm4_register
{
    enum vkd3d_sm4_register_type type;
    uint32_t idx[2];
    unsigned int idx_count;
    enum vkd3d_sm4_dimension dim;
    uint32_t immconst_uint[4];
    unsigned int mod;
};

struct sm4_instruction
{
    enum vkd3d_sm4_opcode opcode;

    struct sm4_instruction_modifier modifiers[1];
    unsigned int modifier_count;

    struct sm4_dst_register
    {
        struct sm4_register reg;
        unsigned int writemask;
    } dsts[2];
    unsigned int dst_count;

    struct sm4_src_register
    {
        struct sm4_register reg;
        enum vkd3d_sm4_swizzle_type swizzle_type;
        unsigned int swizzle;
    } srcs[4];
    unsigned int src_count;

    uint32_t idx[3];
    unsigned int idx_count;
};

static void sm4_register_from_deref(struct hlsl_ctx *ctx, struct sm4_register *reg,
        unsigned int *writemask, enum vkd3d_sm4_swizzle_type *swizzle_type,
        const struct hlsl_deref *deref, const struct hlsl_type *data_type)
{
    const struct hlsl_ir_var *var = deref->var;

    if (var->is_uniform)
    {
        enum hlsl_regset regset = hlsl_type_get_regset(data_type);

        if (regset == HLSL_REGSET_TEXTURES)
        {
            reg->type = VKD3D_SM4_RT_RESOURCE;
            reg->dim = VKD3D_SM4_DIMENSION_VEC4;
            if (swizzle_type)
                *swizzle_type = VKD3D_SM4_SWIZZLE_VEC4;
            reg->idx[0] = var->regs[HLSL_REGSET_TEXTURES].id;
            reg->idx[0] += hlsl_offset_from_deref_safe(ctx, deref);
            assert(deref->offset_regset == HLSL_REGSET_TEXTURES);
            reg->idx_count = 1;
            *writemask = VKD3DSP_WRITEMASK_ALL;
        }
        else if (regset == HLSL_REGSET_UAVS)
        {
            reg->type = VKD3D_SM5_RT_UAV;
            reg->dim = VKD3D_SM4_DIMENSION_VEC4;
            if (swizzle_type)
                *swizzle_type = VKD3D_SM4_SWIZZLE_VEC4;
            reg->idx[0] = var->regs[HLSL_REGSET_UAVS].id;
            reg->idx[0] += hlsl_offset_from_deref_safe(ctx, deref);
            assert(deref->offset_regset == HLSL_REGSET_UAVS);
            reg->idx_count = 1;
            *writemask = VKD3DSP_WRITEMASK_ALL;
        }
        else if (regset == HLSL_REGSET_SAMPLERS)
        {
            reg->type = VKD3D_SM4_RT_SAMPLER;
            reg->dim = VKD3D_SM4_DIMENSION_NONE;
            if (swizzle_type)
                *swizzle_type = VKD3D_SM4_SWIZZLE_NONE;
            reg->idx[0] = var->regs[HLSL_REGSET_SAMPLERS].id;
            reg->idx[0] += hlsl_offset_from_deref_safe(ctx, deref);
            assert(deref->offset_regset == HLSL_REGSET_SAMPLERS);
            reg->idx_count = 1;
            *writemask = VKD3DSP_WRITEMASK_ALL;
        }
        else
        {
            unsigned int offset = hlsl_offset_from_deref_safe(ctx, deref) + var->buffer_offset;

            assert(data_type->class <= HLSL_CLASS_VECTOR);
            reg->type = VKD3D_SM4_RT_CONSTBUFFER;
            reg->dim = VKD3D_SM4_DIMENSION_VEC4;
            if (swizzle_type)
                *swizzle_type = VKD3D_SM4_SWIZZLE_VEC4;
            reg->idx[0] = var->buffer->reg.id;
            reg->idx[1] = offset / 4;
            reg->idx_count = 2;
            *writemask = ((1u << data_type->dimx) - 1) << (offset & 3);
        }
    }
    else if (var->is_input_semantic)
    {
        bool has_idx;

        if (hlsl_sm4_register_from_semantic(ctx, &var->semantic, false, &reg->type, swizzle_type, &has_idx))
        {
            unsigned int offset = hlsl_offset_from_deref_safe(ctx, deref);

            if (has_idx)
            {
                reg->idx[0] = var->semantic.index + offset / 4;
                reg->idx_count = 1;
            }

            reg->dim = VKD3D_SM4_DIMENSION_VEC4;
            *writemask = ((1u << data_type->dimx) - 1) << (offset % 4);
        }
        else
        {
            struct hlsl_reg hlsl_reg = hlsl_reg_from_deref(ctx, deref);

            assert(hlsl_reg.allocated);
            reg->type = VKD3D_SM4_RT_INPUT;
            reg->dim = VKD3D_SM4_DIMENSION_VEC4;
            if (swizzle_type)
                *swizzle_type = VKD3D_SM4_SWIZZLE_VEC4;
            reg->idx[0] = hlsl_reg.id;
            reg->idx_count = 1;
            *writemask = hlsl_reg.writemask;
        }
    }
    else if (var->is_output_semantic)
    {
        bool has_idx;

        if (hlsl_sm4_register_from_semantic(ctx, &var->semantic, true, &reg->type, swizzle_type, &has_idx))
        {
            unsigned int offset = hlsl_offset_from_deref_safe(ctx, deref);

            if (has_idx)
            {
                reg->idx[0] = var->semantic.index + offset / 4;
                reg->idx_count = 1;
            }

            if (reg->type == VKD3D_SM4_RT_DEPTHOUT)
                reg->dim = VKD3D_SM4_DIMENSION_SCALAR;
            else
                reg->dim = VKD3D_SM4_DIMENSION_VEC4;
            *writemask = ((1u << data_type->dimx) - 1) << (offset % 4);
        }
        else
        {
            struct hlsl_reg hlsl_reg = hlsl_reg_from_deref(ctx, deref);

            assert(hlsl_reg.allocated);
            reg->type = VKD3D_SM4_RT_OUTPUT;
            reg->dim = VKD3D_SM4_DIMENSION_VEC4;
            reg->idx[0] = hlsl_reg.id;
            reg->idx_count = 1;
            *writemask = hlsl_reg.writemask;
        }
    }
    else
    {
        struct hlsl_reg hlsl_reg = hlsl_reg_from_deref(ctx, deref);

        assert(hlsl_reg.allocated);
        reg->type = VKD3D_SM4_RT_TEMP;
        reg->dim = VKD3D_SM4_DIMENSION_VEC4;
        if (swizzle_type)
            *swizzle_type = VKD3D_SM4_SWIZZLE_VEC4;
        reg->idx[0] = hlsl_reg.id;
        reg->idx_count = 1;
        *writemask = hlsl_reg.writemask;
    }
}

static void sm4_src_from_deref(struct hlsl_ctx *ctx, struct sm4_src_register *src,
        const struct hlsl_deref *deref, const struct hlsl_type *data_type, unsigned int map_writemask)
{
    unsigned int writemask;

    sm4_register_from_deref(ctx, &src->reg, &writemask, &src->swizzle_type, deref, data_type);
    if (src->swizzle_type == VKD3D_SM4_SWIZZLE_VEC4)
        src->swizzle = hlsl_map_swizzle(hlsl_swizzle_from_writemask(writemask), map_writemask);
}

static void sm4_register_from_node(struct sm4_register *reg, unsigned int *writemask,
        enum vkd3d_sm4_swizzle_type *swizzle_type, const struct hlsl_ir_node *instr)
{
    assert(instr->reg.allocated);
    reg->type = VKD3D_SM4_RT_TEMP;
    reg->dim = VKD3D_SM4_DIMENSION_VEC4;
    *swizzle_type = VKD3D_SM4_SWIZZLE_VEC4;
    reg->idx[0] = instr->reg.id;
    reg->idx_count = 1;
    *writemask = instr->reg.writemask;
}

static void sm4_dst_from_node(struct sm4_dst_register *dst, const struct hlsl_ir_node *instr)
{
    unsigned int swizzle_type;

    sm4_register_from_node(&dst->reg, &dst->writemask, &swizzle_type, instr);
}

static void sm4_src_from_node(struct sm4_src_register *src,
        const struct hlsl_ir_node *instr, unsigned int map_writemask)
{
    unsigned int writemask;

    sm4_register_from_node(&src->reg, &writemask, &src->swizzle_type, instr);
    if (src->swizzle_type == VKD3D_SM4_SWIZZLE_VEC4)
        src->swizzle = hlsl_map_swizzle(hlsl_swizzle_from_writemask(writemask), map_writemask);
}

static void sm4_src_from_constant_value(struct sm4_src_register *src,
        const struct hlsl_constant_value *value, unsigned int width, unsigned int map_writemask)
{
    src->swizzle_type = VKD3D_SM4_SWIZZLE_NONE;
    src->reg.type = VKD3D_SM4_RT_IMMCONST;
    if (width == 1)
    {
        src->reg.dim = VKD3D_SM4_DIMENSION_SCALAR;
        src->reg.immconst_uint[0] = value->u[0].u;
    }
    else
    {
        unsigned int i, j = 0;

        src->reg.dim = VKD3D_SM4_DIMENSION_VEC4;
        for (i = 0; i < 4; ++i)
        {
            if (map_writemask & (1u << i))
                src->reg.immconst_uint[i] = value->u[j++].u;
        }
    }
}

static uint32_t sm4_encode_register(const struct sm4_register *reg)
{
    return (reg->type << VKD3D_SM4_REGISTER_TYPE_SHIFT)
            | (reg->idx_count << VKD3D_SM4_REGISTER_ORDER_SHIFT)
            | (reg->dim << VKD3D_SM4_DIMENSION_SHIFT);
}

static uint32_t sm4_register_order(const struct sm4_register *reg)
{
    uint32_t order = 1;
    if (reg->type == VKD3D_SM4_RT_IMMCONST)
        order += reg->dim == VKD3D_SM4_DIMENSION_VEC4 ? 4 : 1;
    order += reg->idx_count;
    if (reg->mod)
        ++order;
    return order;
}

static void write_sm4_instruction(struct vkd3d_bytecode_buffer *buffer, const struct sm4_instruction *instr)
{
    uint32_t token = instr->opcode;
    unsigned int size = 1, i, j;

    size += instr->modifier_count;
    for (i = 0; i < instr->dst_count; ++i)
        size += sm4_register_order(&instr->dsts[i].reg);
    for (i = 0; i < instr->src_count; ++i)
        size += sm4_register_order(&instr->srcs[i].reg);
    size += instr->idx_count;

    token |= (size << VKD3D_SM4_INSTRUCTION_LENGTH_SHIFT);

    if (instr->modifier_count > 0)
        token |= VKD3D_SM4_INSTRUCTION_MODIFIER;
    put_u32(buffer, token);

    for (i = 0; i < instr->modifier_count; ++i)
    {
        token = sm4_encode_instruction_modifier(&instr->modifiers[i]);
        if (instr->modifier_count > i + 1)
            token |= VKD3D_SM4_INSTRUCTION_MODIFIER;
        put_u32(buffer, token);
    }

    for (i = 0; i < instr->dst_count; ++i)
    {
        token = sm4_encode_register(&instr->dsts[i].reg);
        if (instr->dsts[i].reg.dim == VKD3D_SM4_DIMENSION_VEC4)
            token |= instr->dsts[i].writemask << VKD3D_SM4_WRITEMASK_SHIFT;
        put_u32(buffer, token);

        for (j = 0; j < instr->dsts[i].reg.idx_count; ++j)
            put_u32(buffer, instr->dsts[i].reg.idx[j]);
    }

    for (i = 0; i < instr->src_count; ++i)
    {
        token = sm4_encode_register(&instr->srcs[i].reg);
        token |= (uint32_t)instr->srcs[i].swizzle_type << VKD3D_SM4_SWIZZLE_TYPE_SHIFT;
        token |= instr->srcs[i].swizzle << VKD3D_SM4_SWIZZLE_SHIFT;
        if (instr->srcs[i].reg.mod)
            token |= VKD3D_SM4_EXTENDED_OPERAND;
        put_u32(buffer, token);

        if (instr->srcs[i].reg.mod)
            put_u32(buffer, (instr->srcs[i].reg.mod << VKD3D_SM4_REGISTER_MODIFIER_SHIFT)
                    | VKD3D_SM4_EXTENDED_OPERAND_MODIFIER);

        for (j = 0; j < instr->srcs[i].reg.idx_count; ++j)
            put_u32(buffer, instr->srcs[i].reg.idx[j]);

        if (instr->srcs[i].reg.type == VKD3D_SM4_RT_IMMCONST)
        {
            put_u32(buffer, instr->srcs[i].reg.immconst_uint[0]);
            if (instr->srcs[i].reg.dim == VKD3D_SM4_DIMENSION_VEC4)
            {
                put_u32(buffer, instr->srcs[i].reg.immconst_uint[1]);
                put_u32(buffer, instr->srcs[i].reg.immconst_uint[2]);
                put_u32(buffer, instr->srcs[i].reg.immconst_uint[3]);
            }
        }
    }

    for (j = 0; j < instr->idx_count; ++j)
        put_u32(buffer, instr->idx[j]);
}

static bool encode_texel_offset_as_aoffimmi(struct sm4_instruction *instr,
        const struct hlsl_ir_node *texel_offset)
{
    struct sm4_instruction_modifier modif;
    struct hlsl_ir_constant *offset;

    if (!texel_offset || texel_offset->type != HLSL_IR_CONSTANT)
        return false;
    offset = hlsl_ir_constant(texel_offset);

    modif.type = VKD3D_SM4_MODIFIER_AOFFIMMI;
    modif.u.aoffimmi.u = offset->value.u[0].i;
    modif.u.aoffimmi.v = offset->value.u[1].i;
    modif.u.aoffimmi.w = offset->value.u[2].i;
    if (modif.u.aoffimmi.u < -8 || modif.u.aoffimmi.u > 7
            || modif.u.aoffimmi.v < -8 || modif.u.aoffimmi.v > 7
            || modif.u.aoffimmi.w < -8 || modif.u.aoffimmi.w > 7)
        return false;

    instr->modifiers[instr->modifier_count++] = modif;
    return true;
}

static void write_sm4_dcl_constant_buffer(struct vkd3d_bytecode_buffer *buffer, const struct hlsl_buffer *cbuffer)
{
    const struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM4_OP_DCL_CONSTANT_BUFFER,

        .srcs[0].reg.dim = VKD3D_SM4_DIMENSION_VEC4,
        .srcs[0].reg.type = VKD3D_SM4_RT_CONSTBUFFER,
        .srcs[0].reg.idx = {cbuffer->reg.id, (cbuffer->used_size + 3) / 4},
        .srcs[0].reg.idx_count = 2,
        .srcs[0].swizzle_type = VKD3D_SM4_SWIZZLE_VEC4,
        .srcs[0].swizzle = HLSL_SWIZZLE(X, Y, Z, W),
        .src_count = 1,
    };
    write_sm4_instruction(buffer, &instr);
}

static void write_sm4_dcl_samplers(struct vkd3d_bytecode_buffer *buffer, const struct hlsl_ir_var *var)
{
    unsigned int i, count = var->data_type->reg_size[HLSL_REGSET_SAMPLERS];
    struct sm4_instruction instr;

    for (i = 0; i < count; ++i)
    {
        if (!var->objects_usage[HLSL_REGSET_SAMPLERS][i].used)
            continue;

        instr = (struct sm4_instruction)
        {
            .opcode = VKD3D_SM4_OP_DCL_SAMPLER,

            .dsts[0].reg.type = VKD3D_SM4_RT_SAMPLER,
            .dsts[0].reg.idx = {var->regs[HLSL_REGSET_SAMPLERS].id + i},
            .dsts[0].reg.idx_count = 1,
            .dst_count = 1,
        };

        write_sm4_instruction(buffer, &instr);
    }
}

static void write_sm4_dcl_textures(struct hlsl_ctx *ctx, struct vkd3d_bytecode_buffer *buffer,
        const struct hlsl_ir_var *var, bool uav)
{
    enum hlsl_regset regset = uav ? HLSL_REGSET_UAVS : HLSL_REGSET_TEXTURES;
    unsigned int i, count = var->data_type->reg_size[regset];
    struct hlsl_type *component_type;
    struct sm4_instruction instr;

    component_type = hlsl_type_get_component_type(ctx, var->data_type, 0);

    for (i = 0; i < count; ++i)
    {
        if (!var->objects_usage[regset][i].used)
            continue;

        instr = (struct sm4_instruction)
        {
            .opcode = (uav ? VKD3D_SM5_OP_DCL_UAV_TYPED : VKD3D_SM4_OP_DCL_RESOURCE)
                    | (sm4_resource_dimension(component_type) << VKD3D_SM4_RESOURCE_TYPE_SHIFT),

            .dsts[0].reg.type = uav ? VKD3D_SM5_RT_UAV : VKD3D_SM4_RT_RESOURCE,
            .dsts[0].reg.idx = {var->regs[regset].id + i},
            .dsts[0].reg.idx_count = 1,
            .dst_count = 1,

            .idx[0] = sm4_resource_format(component_type) * 0x1111,
            .idx_count = 1,
        };

        if (component_type->sampler_dim == HLSL_SAMPLER_DIM_2DMS
                || component_type->sampler_dim == HLSL_SAMPLER_DIM_2DMSARRAY)
        {
            instr.opcode |= component_type->sample_count << VKD3D_SM4_RESOURCE_SAMPLE_COUNT_SHIFT;
        }

        write_sm4_instruction(buffer, &instr);
    }
}

static void write_sm4_dcl_semantic(struct hlsl_ctx *ctx, struct vkd3d_bytecode_buffer *buffer, const struct hlsl_ir_var *var)
{
    const struct hlsl_profile_info *profile = ctx->profile;
    const bool output = var->is_output_semantic;
    D3D_NAME usage;
    bool has_idx;

    struct sm4_instruction instr =
    {
        .dsts[0].reg.dim = VKD3D_SM4_DIMENSION_VEC4,
        .dst_count = 1,
    };

    if (hlsl_sm4_register_from_semantic(ctx, &var->semantic, output, &instr.dsts[0].reg.type, NULL, &has_idx))
    {
        if (has_idx)
        {
            instr.dsts[0].reg.idx[0] = var->semantic.index;
            instr.dsts[0].reg.idx_count = 1;
        }
        else
        {
            instr.dsts[0].reg.idx_count = 0;
        }
        instr.dsts[0].writemask = (1 << var->data_type->dimx) - 1;
    }
    else
    {
        instr.dsts[0].reg.type = output ? VKD3D_SM4_RT_OUTPUT : VKD3D_SM4_RT_INPUT;
        instr.dsts[0].reg.idx[0] = var->regs[HLSL_REGSET_NUMERIC].id;
        instr.dsts[0].reg.idx_count = 1;
        instr.dsts[0].writemask = var->regs[HLSL_REGSET_NUMERIC].writemask;
    }

    if (instr.dsts[0].reg.type == VKD3D_SM4_RT_DEPTHOUT)
        instr.dsts[0].reg.dim = VKD3D_SM4_DIMENSION_SCALAR;

    hlsl_sm4_usage_from_semantic(ctx, &var->semantic, output, &usage);
    if (usage == ~0u)
        usage = D3D_NAME_UNDEFINED;

    if (var->is_input_semantic)
    {
        switch (usage)
        {
            case D3D_NAME_UNDEFINED:
                instr.opcode = (profile->type == VKD3D_SHADER_TYPE_PIXEL)
                        ? VKD3D_SM4_OP_DCL_INPUT_PS : VKD3D_SM4_OP_DCL_INPUT;
                break;

            case D3D_NAME_INSTANCE_ID:
            case D3D_NAME_PRIMITIVE_ID:
            case D3D_NAME_VERTEX_ID:
                instr.opcode = (profile->type == VKD3D_SHADER_TYPE_PIXEL)
                        ? VKD3D_SM4_OP_DCL_INPUT_PS_SGV : VKD3D_SM4_OP_DCL_INPUT_SGV;
                break;

            default:
                instr.opcode = (profile->type == VKD3D_SHADER_TYPE_PIXEL)
                        ? VKD3D_SM4_OP_DCL_INPUT_PS_SIV : VKD3D_SM4_OP_DCL_INPUT_SIV;
                break;
        }

        if (profile->type == VKD3D_SHADER_TYPE_PIXEL)
        {
            enum vkd3d_shader_interpolation_mode mode = VKD3DSIM_LINEAR;

            if ((var->storage_modifiers & HLSL_STORAGE_NOINTERPOLATION) || type_is_integer(var->data_type))
                mode = VKD3DSIM_CONSTANT;

            instr.opcode |= mode << VKD3D_SM4_INTERPOLATION_MODE_SHIFT;
        }
    }
    else
    {
        if (usage == D3D_NAME_UNDEFINED || profile->type == VKD3D_SHADER_TYPE_PIXEL)
            instr.opcode = VKD3D_SM4_OP_DCL_OUTPUT;
        else
            instr.opcode = VKD3D_SM4_OP_DCL_OUTPUT_SIV;
    }

    switch (usage)
    {
        case D3D_NAME_COVERAGE:
        case D3D_NAME_DEPTH:
        case D3D_NAME_DEPTH_GREATER_EQUAL:
        case D3D_NAME_DEPTH_LESS_EQUAL:
        case D3D_NAME_TARGET:
        case D3D_NAME_UNDEFINED:
            break;

        default:
            instr.idx_count = 1;
            instr.idx[0] = usage;
            break;
    }

    write_sm4_instruction(buffer, &instr);
}

static void write_sm4_dcl_temps(struct vkd3d_bytecode_buffer *buffer, uint32_t temp_count)
{
    struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM4_OP_DCL_TEMPS,

        .idx = {temp_count},
        .idx_count = 1,
    };

    write_sm4_instruction(buffer, &instr);
}

static void write_sm4_dcl_thread_group(struct vkd3d_bytecode_buffer *buffer, const uint32_t thread_count[3])
{
    struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM5_OP_DCL_THREAD_GROUP,

        .idx = {thread_count[0], thread_count[1], thread_count[2]},
        .idx_count = 3,
    };

    write_sm4_instruction(buffer, &instr);
}

static void write_sm4_ret(struct vkd3d_bytecode_buffer *buffer)
{
    struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM4_OP_RET,
    };

    write_sm4_instruction(buffer, &instr);
}

static void write_sm4_unary_op(struct vkd3d_bytecode_buffer *buffer, enum vkd3d_sm4_opcode opcode,
        const struct hlsl_ir_node *dst, const struct hlsl_ir_node *src, unsigned int src_mod)
{
    struct sm4_instruction instr;

    memset(&instr, 0, sizeof(instr));
    instr.opcode = opcode;

    sm4_dst_from_node(&instr.dsts[0], dst);
    instr.dst_count = 1;

    sm4_src_from_node(&instr.srcs[0], src, instr.dsts[0].writemask);
    instr.srcs[0].reg.mod = src_mod;
    instr.src_count = 1;

    write_sm4_instruction(buffer, &instr);
}

static void write_sm4_unary_op_with_two_destinations(struct vkd3d_bytecode_buffer *buffer,
        enum vkd3d_sm4_opcode opcode, const struct hlsl_ir_node *dst, unsigned dst_idx,
        const struct hlsl_ir_node *src)
{
    struct sm4_instruction instr;

    memset(&instr, 0, sizeof(instr));
    instr.opcode = opcode;

    assert(dst_idx < ARRAY_SIZE(instr.dsts));
    sm4_dst_from_node(&instr.dsts[dst_idx], dst);
    assert(1 - dst_idx >= 0);
    instr.dsts[1 - dst_idx].reg.type = VKD3D_SM4_RT_NULL;
    instr.dsts[1 - dst_idx].reg.dim = VKD3D_SM4_DIMENSION_NONE;
    instr.dsts[1 - dst_idx].reg.idx_count = 0;
    instr.dst_count = 2;

    sm4_src_from_node(&instr.srcs[0], src, instr.dsts[dst_idx].writemask);
    instr.src_count = 1;

    write_sm4_instruction(buffer, &instr);
}

static void write_sm4_binary_op(struct vkd3d_bytecode_buffer *buffer, enum vkd3d_sm4_opcode opcode,
        const struct hlsl_ir_node *dst, const struct hlsl_ir_node *src1, const struct hlsl_ir_node *src2)
{
    struct sm4_instruction instr;

    memset(&instr, 0, sizeof(instr));
    instr.opcode = opcode;

    sm4_dst_from_node(&instr.dsts[0], dst);
    instr.dst_count = 1;

    sm4_src_from_node(&instr.srcs[0], src1, instr.dsts[0].writemask);
    sm4_src_from_node(&instr.srcs[1], src2, instr.dsts[0].writemask);
    instr.src_count = 2;

    write_sm4_instruction(buffer, &instr);
}

/* dp# instructions don't map the swizzle. */
static void write_sm4_binary_op_dot(struct vkd3d_bytecode_buffer *buffer, enum vkd3d_sm4_opcode opcode,
        const struct hlsl_ir_node *dst, const struct hlsl_ir_node *src1, const struct hlsl_ir_node *src2)
{
    struct sm4_instruction instr;

    memset(&instr, 0, sizeof(instr));
    instr.opcode = opcode;

    sm4_dst_from_node(&instr.dsts[0], dst);
    instr.dst_count = 1;

    sm4_src_from_node(&instr.srcs[0], src1, VKD3DSP_WRITEMASK_ALL);
    sm4_src_from_node(&instr.srcs[1], src2, VKD3DSP_WRITEMASK_ALL);
    instr.src_count = 2;

    write_sm4_instruction(buffer, &instr);
}

static void write_sm4_binary_op_with_two_destinations(struct vkd3d_bytecode_buffer *buffer,
        enum vkd3d_sm4_opcode opcode, const struct hlsl_ir_node *dst, unsigned dst_idx,
        const struct hlsl_ir_node *src1, const struct hlsl_ir_node *src2)
{
    struct sm4_instruction instr;

    memset(&instr, 0, sizeof(instr));
    instr.opcode = opcode;

    assert(dst_idx < ARRAY_SIZE(instr.dsts));
    sm4_dst_from_node(&instr.dsts[dst_idx], dst);
    assert(1 - dst_idx >= 0);
    instr.dsts[1 - dst_idx].reg.type = VKD3D_SM4_RT_NULL;
    instr.dsts[1 - dst_idx].reg.dim = VKD3D_SM4_DIMENSION_NONE;
    instr.dsts[1 - dst_idx].reg.idx_count = 0;
    instr.dst_count = 2;

    sm4_src_from_node(&instr.srcs[0], src1, instr.dsts[dst_idx].writemask);
    sm4_src_from_node(&instr.srcs[1], src2, instr.dsts[dst_idx].writemask);
    instr.src_count = 2;

    write_sm4_instruction(buffer, &instr);
}

static void write_sm4_constant(struct hlsl_ctx *ctx,
        struct vkd3d_bytecode_buffer *buffer, const struct hlsl_ir_constant *constant)
{
    const unsigned int dimx = constant->node.data_type->dimx;
    struct sm4_instruction instr;

    memset(&instr, 0, sizeof(instr));
    instr.opcode = VKD3D_SM4_OP_MOV;

    sm4_dst_from_node(&instr.dsts[0], &constant->node);
    instr.dst_count = 1;

    sm4_src_from_constant_value(&instr.srcs[0], &constant->value, dimx, instr.dsts[0].writemask);
    instr.src_count = 1,

    write_sm4_instruction(buffer, &instr);
}

static void write_sm4_ld(struct hlsl_ctx *ctx, struct vkd3d_bytecode_buffer *buffer,
        const struct hlsl_type *resource_type, const struct hlsl_ir_node *dst,
        const struct hlsl_deref *resource, const struct hlsl_ir_node *coords,
        const struct hlsl_ir_node *texel_offset, enum hlsl_sampler_dim dim)
{
    bool uav = (hlsl_type_get_regset(resource_type) == HLSL_REGSET_UAVS);
    struct sm4_instruction instr;
    unsigned int dim_count;

    memset(&instr, 0, sizeof(instr));
    instr.opcode = uav ? VKD3D_SM5_OP_LD_UAV_TYPED : VKD3D_SM4_OP_LD;

    if (texel_offset)
    {
        if (!encode_texel_offset_as_aoffimmi(&instr, texel_offset))
        {
            hlsl_error(ctx, &texel_offset->loc, VKD3D_SHADER_ERROR_HLSL_INVALID_TEXEL_OFFSET,
                    "Offset must resolve to integer literal in the range -8 to 7.");
            return;
        }
    }

    sm4_dst_from_node(&instr.dsts[0], dst);
    instr.dst_count = 1;

    sm4_src_from_node(&instr.srcs[0], coords, VKD3DSP_WRITEMASK_ALL);

    if (!uav)
    {
        /* Mipmap level is in the last component in the IR, but needs to be in the W
         * component in the instruction. */
        dim_count = hlsl_sampler_dim_count(dim);
        if (dim_count == 1)
            instr.srcs[0].swizzle = hlsl_combine_swizzles(instr.srcs[0].swizzle, HLSL_SWIZZLE(X, X, X, Y), 4);
        if (dim_count == 2)
            instr.srcs[0].swizzle = hlsl_combine_swizzles(instr.srcs[0].swizzle, HLSL_SWIZZLE(X, Y, X, Z), 4);
    }

    sm4_src_from_deref(ctx, &instr.srcs[1], resource, resource_type, instr.dsts[0].writemask);

    instr.src_count = 2;

    write_sm4_instruction(buffer, &instr);
}

static void write_sm4_sample(struct hlsl_ctx *ctx, struct vkd3d_bytecode_buffer *buffer,
        const struct hlsl_ir_resource_load *load)
{
    const struct hlsl_type *resource_type = load->resource.var->data_type;
    const struct hlsl_ir_node *texel_offset = load->texel_offset.node;
    const struct hlsl_ir_node *coords = load->coords.node;
    const struct hlsl_deref *resource = &load->resource;
    const struct hlsl_deref *sampler = &load->sampler;
    const struct hlsl_ir_node *dst = &load->node;
    struct sm4_instruction instr;

    memset(&instr, 0, sizeof(instr));
    switch (load->load_type)
    {
        case HLSL_RESOURCE_SAMPLE:
            instr.opcode = VKD3D_SM4_OP_SAMPLE;
            break;

        case HLSL_RESOURCE_SAMPLE_LOD_BIAS:
            instr.opcode = VKD3D_SM4_OP_SAMPLE_B;
            break;

        default:
            vkd3d_unreachable();
    }

    if (texel_offset)
    {
        if (!encode_texel_offset_as_aoffimmi(&instr, texel_offset))
        {
            hlsl_error(ctx, &texel_offset->loc, VKD3D_SHADER_ERROR_HLSL_INVALID_TEXEL_OFFSET,
                    "Offset must resolve to integer literal in the range -8 to 7.");
            return;
        }
    }

    sm4_dst_from_node(&instr.dsts[0], dst);
    instr.dst_count = 1;

    sm4_src_from_node(&instr.srcs[0], coords, VKD3DSP_WRITEMASK_ALL);
    sm4_src_from_deref(ctx, &instr.srcs[1], resource, resource_type, instr.dsts[0].writemask);
    sm4_src_from_deref(ctx, &instr.srcs[2], sampler, sampler->var->data_type, VKD3DSP_WRITEMASK_ALL);
    instr.src_count = 3;

    if (load->load_type == HLSL_RESOURCE_SAMPLE_LOD_BIAS)
    {
        sm4_src_from_node(&instr.srcs[3], load->lod.node, VKD3DSP_WRITEMASK_ALL);
        ++instr.src_count;
    }

    write_sm4_instruction(buffer, &instr);
}

static bool type_is_float(const struct hlsl_type *type)
{
    return type->base_type == HLSL_TYPE_FLOAT || type->base_type == HLSL_TYPE_HALF;
}

static void write_sm4_cast_from_bool(struct hlsl_ctx *ctx,
        struct vkd3d_bytecode_buffer *buffer, const struct hlsl_ir_expr *expr,
        const struct hlsl_ir_node *arg, uint32_t mask)
{
    struct sm4_instruction instr;

    memset(&instr, 0, sizeof(instr));
    instr.opcode = VKD3D_SM4_OP_AND;

    sm4_dst_from_node(&instr.dsts[0], &expr->node);
    instr.dst_count = 1;

    sm4_src_from_node(&instr.srcs[0], arg, instr.dsts[0].writemask);
    instr.srcs[1].swizzle_type = VKD3D_SM4_SWIZZLE_NONE;
    instr.srcs[1].reg.type = VKD3D_SM4_RT_IMMCONST;
    instr.srcs[1].reg.dim = VKD3D_SM4_DIMENSION_SCALAR;
    instr.srcs[1].reg.immconst_uint[0] = mask;
    instr.src_count = 2;

    write_sm4_instruction(buffer, &instr);
}

static void write_sm4_cast(struct hlsl_ctx *ctx,
        struct vkd3d_bytecode_buffer *buffer, const struct hlsl_ir_expr *expr)
{
    static const union
    {
        uint32_t u;
        float f;
    } one = { .f = 1.0 };
    const struct hlsl_ir_node *arg1 = expr->operands[0].node;
    const struct hlsl_type *dst_type = expr->node.data_type;
    const struct hlsl_type *src_type = arg1->data_type;

    /* Narrowing casts were already lowered. */
    assert(src_type->dimx == dst_type->dimx);

    switch (dst_type->base_type)
    {
        case HLSL_TYPE_HALF:
        case HLSL_TYPE_FLOAT:
            switch (src_type->base_type)
            {
                case HLSL_TYPE_HALF:
                case HLSL_TYPE_FLOAT:
                    write_sm4_unary_op(buffer, VKD3D_SM4_OP_MOV, &expr->node, arg1, 0);
                    break;

                case HLSL_TYPE_INT:
                    write_sm4_unary_op(buffer, VKD3D_SM4_OP_ITOF, &expr->node, arg1, 0);
                    break;

                case HLSL_TYPE_UINT:
                    write_sm4_unary_op(buffer, VKD3D_SM4_OP_UTOF, &expr->node, arg1, 0);
                    break;

                case HLSL_TYPE_BOOL:
                    write_sm4_cast_from_bool(ctx, buffer, expr, arg1, one.u);
                    break;

                case HLSL_TYPE_DOUBLE:
                    hlsl_fixme(ctx, &expr->node.loc, "SM4 cast from double to float.");
                    break;

                default:
                    vkd3d_unreachable();
            }
            break;

        case HLSL_TYPE_INT:
            switch (src_type->base_type)
            {
                case HLSL_TYPE_HALF:
                case HLSL_TYPE_FLOAT:
                    write_sm4_unary_op(buffer, VKD3D_SM4_OP_FTOI, &expr->node, arg1, 0);
                    break;

                case HLSL_TYPE_INT:
                case HLSL_TYPE_UINT:
                    write_sm4_unary_op(buffer, VKD3D_SM4_OP_MOV, &expr->node, arg1, 0);
                    break;

                case HLSL_TYPE_BOOL:
                    write_sm4_cast_from_bool(ctx, buffer, expr, arg1, 1);
                    break;

                case HLSL_TYPE_DOUBLE:
                    hlsl_fixme(ctx, &expr->node.loc, "SM4 cast from double to int.");
                    break;

                default:
                    vkd3d_unreachable();
            }
            break;

        case HLSL_TYPE_UINT:
            switch (src_type->base_type)
            {
                case HLSL_TYPE_HALF:
                case HLSL_TYPE_FLOAT:
                    write_sm4_unary_op(buffer, VKD3D_SM4_OP_FTOU, &expr->node, arg1, 0);
                    break;

                case HLSL_TYPE_INT:
                case HLSL_TYPE_UINT:
                    write_sm4_unary_op(buffer, VKD3D_SM4_OP_MOV, &expr->node, arg1, 0);
                    break;

                case HLSL_TYPE_BOOL:
                    write_sm4_cast_from_bool(ctx, buffer, expr, arg1, 1);
                    break;

                case HLSL_TYPE_DOUBLE:
                    hlsl_fixme(ctx, &expr->node.loc, "SM4 cast from double to uint.");
                    break;

                default:
                    vkd3d_unreachable();
            }
            break;

        case HLSL_TYPE_DOUBLE:
            hlsl_fixme(ctx, &expr->node.loc, "SM4 cast to double.");
            break;

        case HLSL_TYPE_BOOL:
            /* Casts to bool should have already been lowered. */
        default:
            vkd3d_unreachable();
    }
}

static void write_sm4_store_uav_typed(struct hlsl_ctx *ctx, struct vkd3d_bytecode_buffer *buffer,
        const struct hlsl_deref *dst, const struct hlsl_ir_node *coords, const struct hlsl_ir_node *value)
{
    struct sm4_instruction instr;

    memset(&instr, 0, sizeof(instr));
    instr.opcode = VKD3D_SM5_OP_STORE_UAV_TYPED;

    sm4_register_from_deref(ctx, &instr.dsts[0].reg, &instr.dsts[0].writemask, NULL, dst, dst->var->data_type);
    instr.dst_count = 1;

    sm4_src_from_node(&instr.srcs[0], coords, VKD3DSP_WRITEMASK_ALL);
    sm4_src_from_node(&instr.srcs[1], value, VKD3DSP_WRITEMASK_ALL);
    instr.src_count = 2;

    write_sm4_instruction(buffer, &instr);
}

static void write_sm4_expr(struct hlsl_ctx *ctx,
        struct vkd3d_bytecode_buffer *buffer, const struct hlsl_ir_expr *expr)
{
    const struct hlsl_ir_node *arg1 = expr->operands[0].node;
    const struct hlsl_ir_node *arg2 = expr->operands[1].node;
    const struct hlsl_type *dst_type = expr->node.data_type;
    struct vkd3d_string_buffer *dst_type_string;

    assert(expr->node.reg.allocated);

    if (!(dst_type_string = hlsl_type_to_string(ctx, dst_type)))
        return;

    switch (expr->op)
    {
        case HLSL_OP1_ABS:
            switch (dst_type->base_type)
            {
                case HLSL_TYPE_FLOAT:
                    write_sm4_unary_op(buffer, VKD3D_SM4_OP_MOV, &expr->node, arg1, VKD3D_SM4_REGISTER_MODIFIER_ABS);
                    break;

                default:
                    hlsl_fixme(ctx, &expr->node.loc, "SM4 %s absolute value expression.", dst_type_string->buffer);
            }
            break;

        case HLSL_OP1_BIT_NOT:
            assert(type_is_integer(dst_type));
            write_sm4_unary_op(buffer, VKD3D_SM4_OP_NOT, &expr->node, arg1, 0);
            break;

        case HLSL_OP1_CAST:
            write_sm4_cast(ctx, buffer, expr);
            break;

        case HLSL_OP1_COS:
            assert(type_is_float(dst_type));
            write_sm4_unary_op_with_two_destinations(buffer, VKD3D_SM4_OP_SINCOS, &expr->node, 1, arg1);
            break;

        case HLSL_OP1_DSX:
            assert(type_is_float(dst_type));
            write_sm4_unary_op(buffer, VKD3D_SM4_OP_DERIV_RTX, &expr->node, arg1, 0);
            break;

        case HLSL_OP1_DSY:
            assert(type_is_float(dst_type));
            write_sm4_unary_op(buffer, VKD3D_SM4_OP_DERIV_RTY, &expr->node, arg1, 0);
            break;

        case HLSL_OP1_EXP2:
            assert(type_is_float(dst_type));
            write_sm4_unary_op(buffer, VKD3D_SM4_OP_EXP, &expr->node, arg1, 0);
            break;

        case HLSL_OP1_FLOOR:
            assert(type_is_float(dst_type));
            write_sm4_unary_op(buffer, VKD3D_SM4_OP_ROUND_NI, &expr->node, arg1, 0);
            break;

        case HLSL_OP1_FRACT:
            assert(type_is_float(dst_type));
            write_sm4_unary_op(buffer, VKD3D_SM4_OP_FRC, &expr->node, arg1, 0);
            break;

        case HLSL_OP1_LOG2:
            assert(type_is_float(dst_type));
            write_sm4_unary_op(buffer, VKD3D_SM4_OP_LOG, &expr->node, arg1, 0);
            break;

        case HLSL_OP1_LOGIC_NOT:
            assert(dst_type->base_type == HLSL_TYPE_BOOL);
            write_sm4_unary_op(buffer, VKD3D_SM4_OP_NOT, &expr->node, arg1, 0);
            break;

        case HLSL_OP1_NEG:
            switch (dst_type->base_type)
            {
                case HLSL_TYPE_FLOAT:
                    write_sm4_unary_op(buffer, VKD3D_SM4_OP_MOV, &expr->node, arg1, VKD3D_SM4_REGISTER_MODIFIER_NEGATE);
                    break;

                case HLSL_TYPE_INT:
                case HLSL_TYPE_UINT:
                    write_sm4_unary_op(buffer, VKD3D_SM4_OP_INEG, &expr->node, arg1, 0);
                    break;

                default:
                    hlsl_fixme(ctx, &expr->node.loc, "SM4 %s negation expression.", dst_type_string->buffer);
            }
            break;

        case HLSL_OP1_REINTERPRET:
            write_sm4_unary_op(buffer, VKD3D_SM4_OP_MOV, &expr->node, arg1, 0);
            break;

        case HLSL_OP1_ROUND:
            assert(type_is_float(dst_type));
            write_sm4_unary_op(buffer, VKD3D_SM4_OP_ROUND_NE, &expr->node, arg1, 0);
            break;

        case HLSL_OP1_RSQ:
            assert(type_is_float(dst_type));
            write_sm4_unary_op(buffer, VKD3D_SM4_OP_RSQ, &expr->node, arg1, 0);
            break;

        case HLSL_OP1_SAT:
            assert(type_is_float(dst_type));
            write_sm4_unary_op(buffer, VKD3D_SM4_OP_MOV
                    | (VKD3D_SM4_INSTRUCTION_FLAG_SATURATE << VKD3D_SM4_INSTRUCTION_FLAGS_SHIFT),
                    &expr->node, arg1, 0);
            break;

        case HLSL_OP1_SIN:
            assert(type_is_float(dst_type));
            write_sm4_unary_op_with_two_destinations(buffer, VKD3D_SM4_OP_SINCOS, &expr->node, 0, arg1);
            break;

        case HLSL_OP1_SQRT:
            assert(type_is_float(dst_type));
            write_sm4_unary_op(buffer, VKD3D_SM4_OP_SQRT, &expr->node, arg1, 0);
            break;

        case HLSL_OP1_TRUNC:
            assert(type_is_float(dst_type));
            write_sm4_unary_op(buffer, VKD3D_SM4_OP_ROUND_Z, &expr->node, arg1, 0);
            break;

        case HLSL_OP2_ADD:
            switch (dst_type->base_type)
            {
                case HLSL_TYPE_FLOAT:
                    write_sm4_binary_op(buffer, VKD3D_SM4_OP_ADD, &expr->node, arg1, arg2);
                    break;

                case HLSL_TYPE_INT:
                case HLSL_TYPE_UINT:
                    write_sm4_binary_op(buffer, VKD3D_SM4_OP_IADD, &expr->node, arg1, arg2);
                    break;

                default:
                    hlsl_fixme(ctx, &expr->node.loc, "SM4 %s addition expression.", dst_type_string->buffer);
            }
            break;

        case HLSL_OP2_BIT_AND:
            assert(type_is_integer(dst_type));
            write_sm4_binary_op(buffer, VKD3D_SM4_OP_AND, &expr->node, arg1, arg2);
            break;

        case HLSL_OP2_BIT_OR:
            assert(type_is_integer(dst_type));
            write_sm4_binary_op(buffer, VKD3D_SM4_OP_OR, &expr->node, arg1, arg2);
            break;

        case HLSL_OP2_BIT_XOR:
            assert(type_is_integer(dst_type));
            write_sm4_binary_op(buffer, VKD3D_SM4_OP_XOR, &expr->node, arg1, arg2);
            break;

        case HLSL_OP2_DIV:
            switch (dst_type->base_type)
            {
                case HLSL_TYPE_FLOAT:
                    write_sm4_binary_op(buffer, VKD3D_SM4_OP_DIV, &expr->node, arg1, arg2);
                    break;

                case HLSL_TYPE_UINT:
                    write_sm4_binary_op_with_two_destinations(buffer, VKD3D_SM4_OP_UDIV, &expr->node, 0, arg1, arg2);
                    break;

                default:
                    hlsl_fixme(ctx, &expr->node.loc, "SM4 %s division expression.", dst_type_string->buffer);
            }
            break;

        case HLSL_OP2_DOT:
            switch (dst_type->base_type)
            {
                case HLSL_TYPE_FLOAT:
                    switch (arg1->data_type->dimx)
                    {
                        case 4:
                            write_sm4_binary_op_dot(buffer, VKD3D_SM4_OP_DP4, &expr->node, arg1, arg2);
                            break;

                        case 3:
                            write_sm4_binary_op_dot(buffer, VKD3D_SM4_OP_DP3, &expr->node, arg1, arg2);
                            break;

                        case 2:
                            write_sm4_binary_op_dot(buffer, VKD3D_SM4_OP_DP2, &expr->node, arg1, arg2);
                            break;

                        case 1:
                        default:
                            vkd3d_unreachable();
                    }
                    break;

                default:
                    hlsl_fixme(ctx, &expr->node.loc, "SM4 %s dot expression.", dst_type_string->buffer);
            }
            break;

        case HLSL_OP2_EQUAL:
        {
            const struct hlsl_type *src_type = arg1->data_type;

            assert(dst_type->base_type == HLSL_TYPE_BOOL);

            switch (src_type->base_type)
            {
                case HLSL_TYPE_FLOAT:
                    write_sm4_binary_op(buffer, VKD3D_SM4_OP_EQ, &expr->node, arg1, arg2);
                    break;

                case HLSL_TYPE_BOOL:
                case HLSL_TYPE_INT:
                case HLSL_TYPE_UINT:
                    write_sm4_binary_op(buffer, VKD3D_SM4_OP_IEQ, &expr->node, arg1, arg2);
                    break;

                default:
                    hlsl_fixme(ctx, &expr->node.loc, "SM4 equality between \"%s\" operands.",
                            debug_hlsl_type(ctx, src_type));
                    break;
            }
            break;
        }

        case HLSL_OP2_GEQUAL:
        {
            const struct hlsl_type *src_type = arg1->data_type;

            assert(dst_type->base_type == HLSL_TYPE_BOOL);

            switch (src_type->base_type)
            {
                case HLSL_TYPE_FLOAT:
                    write_sm4_binary_op(buffer, VKD3D_SM4_OP_GE, &expr->node, arg1, arg2);
                    break;

                case HLSL_TYPE_INT:
                    write_sm4_binary_op(buffer, VKD3D_SM4_OP_IGE, &expr->node, arg1, arg2);
                    break;

                case HLSL_TYPE_BOOL:
                case HLSL_TYPE_UINT:
                    write_sm4_binary_op(buffer, VKD3D_SM4_OP_UGE, &expr->node, arg1, arg2);
                    break;

                default:
                    hlsl_fixme(ctx, &expr->node.loc, "SM4 greater-than-or-equal between \"%s\" operands.",
                            debug_hlsl_type(ctx, src_type));
                    break;
            }
            break;
        }

        case HLSL_OP2_LESS:
        {
            const struct hlsl_type *src_type = arg1->data_type;

            assert(dst_type->base_type == HLSL_TYPE_BOOL);

            switch (src_type->base_type)
            {
                case HLSL_TYPE_FLOAT:
                    write_sm4_binary_op(buffer, VKD3D_SM4_OP_LT, &expr->node, arg1, arg2);
                    break;

                case HLSL_TYPE_INT:
                    write_sm4_binary_op(buffer, VKD3D_SM4_OP_ILT, &expr->node, arg1, arg2);
                    break;

                case HLSL_TYPE_BOOL:
                case HLSL_TYPE_UINT:
                    write_sm4_binary_op(buffer, VKD3D_SM4_OP_ULT, &expr->node, arg1, arg2);
                    break;

                default:
                    hlsl_fixme(ctx, &expr->node.loc, "SM4 less-than between \"%s\" operands.",
                            debug_hlsl_type(ctx, src_type));
                    break;
            }
            break;
        }

        case HLSL_OP2_LOGIC_AND:
            assert(dst_type->base_type == HLSL_TYPE_BOOL);
            write_sm4_binary_op(buffer, VKD3D_SM4_OP_AND, &expr->node, arg1, arg2);
            break;

        case HLSL_OP2_LOGIC_OR:
            assert(dst_type->base_type == HLSL_TYPE_BOOL);
            write_sm4_binary_op(buffer, VKD3D_SM4_OP_OR, &expr->node, arg1, arg2);
            break;

        case HLSL_OP2_LSHIFT:
            assert(type_is_integer(dst_type));
            assert(dst_type->base_type != HLSL_TYPE_BOOL);
            write_sm4_binary_op(buffer, VKD3D_SM4_OP_ISHL, &expr->node, arg1, arg2);
            break;

        case HLSL_OP2_MAX:
            switch (dst_type->base_type)
            {
                case HLSL_TYPE_FLOAT:
                    write_sm4_binary_op(buffer, VKD3D_SM4_OP_MAX, &expr->node, arg1, arg2);
                    break;

                case HLSL_TYPE_INT:
                    write_sm4_binary_op(buffer, VKD3D_SM4_OP_IMAX, &expr->node, arg1, arg2);
                    break;

                case HLSL_TYPE_UINT:
                    write_sm4_binary_op(buffer, VKD3D_SM4_OP_UMAX, &expr->node, arg1, arg2);
                    break;

                default:
                    hlsl_fixme(ctx, &expr->node.loc, "SM4 %s maximum expression.", dst_type_string->buffer);
            }
            break;

        case HLSL_OP2_MIN:
            switch (dst_type->base_type)
            {
                case HLSL_TYPE_FLOAT:
                    write_sm4_binary_op(buffer, VKD3D_SM4_OP_MIN, &expr->node, arg1, arg2);
                    break;

                case HLSL_TYPE_INT:
                    write_sm4_binary_op(buffer, VKD3D_SM4_OP_IMIN, &expr->node, arg1, arg2);
                    break;

                case HLSL_TYPE_UINT:
                    write_sm4_binary_op(buffer, VKD3D_SM4_OP_UMIN, &expr->node, arg1, arg2);
                    break;

                default:
                    hlsl_fixme(ctx, &expr->node.loc, "SM4 %s minimum expression.", dst_type_string->buffer);
            }
            break;

        case HLSL_OP2_MOD:
            switch (dst_type->base_type)
            {
                case HLSL_TYPE_UINT:
                    write_sm4_binary_op_with_two_destinations(buffer, VKD3D_SM4_OP_UDIV, &expr->node, 1, arg1, arg2);
                    break;

                default:
                    hlsl_fixme(ctx, &expr->node.loc, "SM4 %s modulus expression.", dst_type_string->buffer);
            }
            break;

        case HLSL_OP2_MUL:
            switch (dst_type->base_type)
            {
                case HLSL_TYPE_FLOAT:
                    write_sm4_binary_op(buffer, VKD3D_SM4_OP_MUL, &expr->node, arg1, arg2);
                    break;

                case HLSL_TYPE_INT:
                case HLSL_TYPE_UINT:
                    /* Using IMUL instead of UMUL because we're taking the low
                     * bits, and the native compiler generates IMUL. */
                    write_sm4_binary_op_with_two_destinations(buffer, VKD3D_SM4_OP_IMUL, &expr->node, 1, arg1, arg2);
                    break;

                default:
                    hlsl_fixme(ctx, &expr->node.loc, "SM4 %s multiplication expression.", dst_type_string->buffer);
            }
            break;

        case HLSL_OP2_NEQUAL:
        {
            const struct hlsl_type *src_type = arg1->data_type;

            assert(dst_type->base_type == HLSL_TYPE_BOOL);

            switch (src_type->base_type)
            {
                case HLSL_TYPE_FLOAT:
                    write_sm4_binary_op(buffer, VKD3D_SM4_OP_NE, &expr->node, arg1, arg2);
                    break;

                case HLSL_TYPE_BOOL:
                case HLSL_TYPE_INT:
                case HLSL_TYPE_UINT:
                    write_sm4_binary_op(buffer, VKD3D_SM4_OP_INE, &expr->node, arg1, arg2);
                    break;

                default:
                    hlsl_fixme(ctx, &expr->node.loc, "SM4 inequality between \"%s\" operands.",
                            debug_hlsl_type(ctx, src_type));
                    break;
            }
            break;
        }

        case HLSL_OP2_RSHIFT:
            assert(type_is_integer(dst_type));
            assert(dst_type->base_type != HLSL_TYPE_BOOL);
            write_sm4_binary_op(buffer, dst_type->base_type == HLSL_TYPE_INT ? VKD3D_SM4_OP_ISHR : VKD3D_SM4_OP_USHR,
                    &expr->node, arg1, arg2);
            break;

        default:
            hlsl_fixme(ctx, &expr->node.loc, "SM4 %s expression.", debug_hlsl_expr_op(expr->op));
    }

    hlsl_release_string_buffer(ctx, dst_type_string);
}

static void write_sm4_if(struct hlsl_ctx *ctx, struct vkd3d_bytecode_buffer *buffer, const struct hlsl_ir_if *iff)
{
    struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM4_OP_IF | VKD3D_SM4_CONDITIONAL_NZ,
        .src_count = 1,
    };

    assert(iff->condition.node->data_type->dimx == 1);

    sm4_src_from_node(&instr.srcs[0], iff->condition.node, VKD3DSP_WRITEMASK_ALL);
    write_sm4_instruction(buffer, &instr);

    write_sm4_block(ctx, buffer, &iff->then_block);

    if (!list_empty(&iff->else_block.instrs))
    {
        instr.opcode = VKD3D_SM4_OP_ELSE;
        instr.src_count = 0;
        write_sm4_instruction(buffer, &instr);

        write_sm4_block(ctx, buffer, &iff->else_block);
    }

    instr.opcode = VKD3D_SM4_OP_ENDIF;
    instr.src_count = 0;
    write_sm4_instruction(buffer, &instr);
}

static void write_sm4_jump(struct hlsl_ctx *ctx,
        struct vkd3d_bytecode_buffer *buffer, const struct hlsl_ir_jump *jump)
{
    struct sm4_instruction instr = {0};

    switch (jump->type)
    {
        case HLSL_IR_JUMP_BREAK:
            instr.opcode = VKD3D_SM4_OP_BREAK;
            break;

        case HLSL_IR_JUMP_DISCARD:
        {
            struct sm4_register *reg = &instr.srcs[0].reg;

            instr.opcode = VKD3D_SM4_OP_DISCARD | VKD3D_SM4_CONDITIONAL_NZ;

            memset(&instr.srcs[0], 0, sizeof(*instr.srcs));
            instr.srcs[0].swizzle_type = VKD3D_SM4_SWIZZLE_NONE;
            instr.src_count = 1;
            reg->type = VKD3D_SM4_RT_IMMCONST;
            reg->dim = VKD3D_SM4_DIMENSION_SCALAR;
            reg->immconst_uint[0] = ~0u;

            break;
        }

        case HLSL_IR_JUMP_RETURN:
            vkd3d_unreachable();

        default:
            hlsl_fixme(ctx, &jump->node.loc, "Jump type %s.\n", hlsl_jump_type_to_string(jump->type));
            return;
    }

    write_sm4_instruction(buffer, &instr);
}

/* Does this variable's data come directly from the API user, rather than being
 * temporary or from a previous shader stage?
 * I.e. is it a uniform or VS input? */
static bool var_is_user_input(struct hlsl_ctx *ctx, const struct hlsl_ir_var *var)
{
    if (var->is_uniform)
        return true;

    return var->is_input_semantic && ctx->profile->type == VKD3D_SHADER_TYPE_VERTEX;
}

static void write_sm4_load(struct hlsl_ctx *ctx,
        struct vkd3d_bytecode_buffer *buffer, const struct hlsl_ir_load *load)
{
    const struct hlsl_type *type = load->node.data_type;
    struct sm4_instruction instr;

    memset(&instr, 0, sizeof(instr));

    sm4_dst_from_node(&instr.dsts[0], &load->node);
    instr.dst_count = 1;

    assert(type->class <= HLSL_CLASS_LAST_NUMERIC);
    if (type->base_type == HLSL_TYPE_BOOL && var_is_user_input(ctx, load->src.var))
    {
        struct hlsl_constant_value value;

        /* Uniform bools can be specified as anything, but internal bools always
         * have 0 for false and ~0 for true. Normalize that here. */

        instr.opcode = VKD3D_SM4_OP_MOVC;

        sm4_src_from_deref(ctx, &instr.srcs[0], &load->src, type, instr.dsts[0].writemask);

        memset(&value, 0xff, sizeof(value));
        sm4_src_from_constant_value(&instr.srcs[1], &value, type->dimx, instr.dsts[0].writemask);
        memset(&value, 0, sizeof(value));
        sm4_src_from_constant_value(&instr.srcs[2], &value, type->dimx, instr.dsts[0].writemask);
        instr.src_count = 3;
    }
    else
    {
        instr.opcode = VKD3D_SM4_OP_MOV;

        sm4_src_from_deref(ctx, &instr.srcs[0], &load->src, type, instr.dsts[0].writemask);
        instr.src_count = 1;
    }

    write_sm4_instruction(buffer, &instr);
}

static void write_sm4_loop(struct hlsl_ctx *ctx,
        struct vkd3d_bytecode_buffer *buffer, const struct hlsl_ir_loop *loop)
{
    struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM4_OP_LOOP,
    };

    write_sm4_instruction(buffer, &instr);

    write_sm4_block(ctx, buffer, &loop->body);

    instr.opcode = VKD3D_SM4_OP_ENDLOOP;
    write_sm4_instruction(buffer, &instr);
}

static void write_sm4_gather(struct hlsl_ctx *ctx, struct vkd3d_bytecode_buffer *buffer,
        const struct hlsl_type *resource_type, const struct hlsl_ir_node *dst,
        const struct hlsl_deref *resource, const struct hlsl_deref *sampler,
        const struct hlsl_ir_node *coords, unsigned int swizzle, const struct hlsl_ir_node *texel_offset)
{
    struct sm4_src_register *src;
    struct sm4_instruction instr;

    memset(&instr, 0, sizeof(instr));

    instr.opcode = VKD3D_SM4_OP_GATHER4;

    sm4_dst_from_node(&instr.dsts[0], dst);
    instr.dst_count = 1;

    sm4_src_from_node(&instr.srcs[instr.src_count++], coords, VKD3DSP_WRITEMASK_ALL);

    if (texel_offset)
    {
        if (!encode_texel_offset_as_aoffimmi(&instr, texel_offset))
        {
            if (ctx->profile->major_version < 5)
            {
                hlsl_error(ctx, &texel_offset->loc, VKD3D_SHADER_ERROR_HLSL_INVALID_TEXEL_OFFSET,
                    "Offset must resolve to integer literal in the range -8 to 7 for profiles < 5.");
                return;
            }
            instr.opcode = VKD3D_SM5_OP_GATHER4_PO;
            sm4_src_from_node(&instr.srcs[instr.src_count++], texel_offset, VKD3DSP_WRITEMASK_ALL);
        }
    }

    sm4_src_from_deref(ctx, &instr.srcs[instr.src_count++], resource, resource_type, instr.dsts[0].writemask);

    src = &instr.srcs[instr.src_count++];
    sm4_src_from_deref(ctx, src, sampler, sampler->var->data_type, VKD3DSP_WRITEMASK_ALL);
    src->reg.dim = VKD3D_SM4_DIMENSION_VEC4;
    src->swizzle_type = VKD3D_SM4_SWIZZLE_SCALAR;
    src->swizzle = swizzle;

    write_sm4_instruction(buffer, &instr);
}

static void write_sm4_resource_load(struct hlsl_ctx *ctx,
        struct vkd3d_bytecode_buffer *buffer, const struct hlsl_ir_resource_load *load)
{
    const struct hlsl_type *resource_type = load->resource.var->data_type;
    const struct hlsl_ir_node *texel_offset = load->texel_offset.node;
    const struct hlsl_ir_node *coords = load->coords.node;

    if (!hlsl_type_is_resource(resource_type))
    {
        hlsl_fixme(ctx, &load->node.loc, "Separate object fields as new variables.");
        return;
    }

    if (load->sampler.var)
    {
        const struct hlsl_type *sampler_type = load->sampler.var->data_type;

        if (!hlsl_type_is_resource(sampler_type))
        {
            hlsl_fixme(ctx, &load->node.loc, "Separate object fields as new variables.");
            return;
        }

        if (!load->sampler.var->is_uniform)
        {
            hlsl_fixme(ctx, &load->node.loc, "Sample using non-uniform sampler variable.");
            return;
        }
    }

    if (!load->resource.var->is_uniform)
    {
        hlsl_fixme(ctx, &load->node.loc, "Load from non-uniform resource variable.");
        return;
    }

    switch (load->load_type)
    {
        case HLSL_RESOURCE_LOAD:
            write_sm4_ld(ctx, buffer, resource_type, &load->node, &load->resource,
                    coords, texel_offset, load->sampling_dim);
            break;

        case HLSL_RESOURCE_SAMPLE:
        case HLSL_RESOURCE_SAMPLE_LOD_BIAS:
            if (!load->sampler.var)
            {
                hlsl_fixme(ctx, &load->node.loc, "SM4 combined sample expression.");
                return;
            }
            write_sm4_sample(ctx, buffer, load);
            break;

        case HLSL_RESOURCE_GATHER_RED:
            write_sm4_gather(ctx, buffer, resource_type, &load->node, &load->resource,
                    &load->sampler, coords, HLSL_SWIZZLE(X, X, X, X), texel_offset);
            break;

        case HLSL_RESOURCE_GATHER_GREEN:
            write_sm4_gather(ctx, buffer, resource_type, &load->node, &load->resource,
                    &load->sampler, coords, HLSL_SWIZZLE(Y, Y, Y, Y), texel_offset);
            break;

        case HLSL_RESOURCE_GATHER_BLUE:
            write_sm4_gather(ctx, buffer, resource_type, &load->node, &load->resource,
                    &load->sampler, coords, HLSL_SWIZZLE(Z, Z, Z, Z), texel_offset);
            break;

        case HLSL_RESOURCE_GATHER_ALPHA:
            write_sm4_gather(ctx, buffer, resource_type, &load->node, &load->resource,
                    &load->sampler, coords, HLSL_SWIZZLE(W, W, W, W), texel_offset);
            break;

        case HLSL_RESOURCE_SAMPLE_LOD:
            hlsl_fixme(ctx, &load->node.loc, "SM4 sample-LOD expression.");
            break;
    }
}

static void write_sm4_resource_store(struct hlsl_ctx *ctx,
        struct vkd3d_bytecode_buffer *buffer, const struct hlsl_ir_resource_store *store)
{
    const struct hlsl_type *resource_type = store->resource.var->data_type;

    if (!hlsl_type_is_resource(resource_type))
    {
        hlsl_fixme(ctx, &store->node.loc, "Separate object fields as new variables.");
        return;
    }

    if (!store->resource.var->is_uniform)
    {
        hlsl_fixme(ctx, &store->node.loc, "Store to non-uniform resource variable.");
        return;
    }

    write_sm4_store_uav_typed(ctx, buffer, &store->resource, store->coords.node, store->value.node);
}

static void write_sm4_store(struct hlsl_ctx *ctx,
        struct vkd3d_bytecode_buffer *buffer, const struct hlsl_ir_store *store)
{
    const struct hlsl_ir_node *rhs = store->rhs.node;
    struct sm4_instruction instr;
    unsigned int writemask;

    memset(&instr, 0, sizeof(instr));
    instr.opcode = VKD3D_SM4_OP_MOV;

    sm4_register_from_deref(ctx, &instr.dsts[0].reg, &writemask, NULL, &store->lhs, rhs->data_type);
    instr.dsts[0].writemask = hlsl_combine_writemasks(writemask, store->writemask);
    instr.dst_count = 1;

    sm4_src_from_node(&instr.srcs[0], rhs, instr.dsts[0].writemask);
    instr.src_count = 1;

    write_sm4_instruction(buffer, &instr);
}

static void write_sm4_swizzle(struct hlsl_ctx *ctx,
        struct vkd3d_bytecode_buffer *buffer, const struct hlsl_ir_swizzle *swizzle)
{
    struct sm4_instruction instr;
    unsigned int writemask;

    memset(&instr, 0, sizeof(instr));
    instr.opcode = VKD3D_SM4_OP_MOV;

    sm4_dst_from_node(&instr.dsts[0], &swizzle->node);
    instr.dst_count = 1;

    sm4_register_from_node(&instr.srcs[0].reg, &writemask, &instr.srcs[0].swizzle_type, swizzle->val.node);
    instr.srcs[0].swizzle = hlsl_map_swizzle(hlsl_combine_swizzles(hlsl_swizzle_from_writemask(writemask),
            swizzle->swizzle, swizzle->node.data_type->dimx), instr.dsts[0].writemask);
    instr.src_count = 1;

    write_sm4_instruction(buffer, &instr);
}

static void write_sm4_block(struct hlsl_ctx *ctx, struct vkd3d_bytecode_buffer *buffer,
        const struct hlsl_block *block)
{
    const struct hlsl_ir_node *instr;

    LIST_FOR_EACH_ENTRY(instr, &block->instrs, struct hlsl_ir_node, entry)
    {
        if (instr->data_type)
        {
            if (instr->data_type->class == HLSL_CLASS_MATRIX)
            {
                hlsl_fixme(ctx, &instr->loc, "Matrix operations need to be lowered.");
                break;
            }
            else if (instr->data_type->class == HLSL_CLASS_OBJECT)
            {
                hlsl_fixme(ctx, &instr->loc, "Object copy.");
                break;
            }

            assert(instr->data_type->class == HLSL_CLASS_SCALAR || instr->data_type->class == HLSL_CLASS_VECTOR);
        }

        switch (instr->type)
        {
            case HLSL_IR_CALL:
                vkd3d_unreachable();

            case HLSL_IR_CONSTANT:
                write_sm4_constant(ctx, buffer, hlsl_ir_constant(instr));
                break;

            case HLSL_IR_EXPR:
                write_sm4_expr(ctx, buffer, hlsl_ir_expr(instr));
                break;

            case HLSL_IR_IF:
                write_sm4_if(ctx, buffer, hlsl_ir_if(instr));
                break;

            case HLSL_IR_JUMP:
                write_sm4_jump(ctx, buffer, hlsl_ir_jump(instr));
                break;

            case HLSL_IR_LOAD:
                write_sm4_load(ctx, buffer, hlsl_ir_load(instr));
                break;

            case HLSL_IR_RESOURCE_LOAD:
                write_sm4_resource_load(ctx, buffer, hlsl_ir_resource_load(instr));
                break;

            case HLSL_IR_RESOURCE_STORE:
                write_sm4_resource_store(ctx, buffer, hlsl_ir_resource_store(instr));
                break;

            case HLSL_IR_LOOP:
                write_sm4_loop(ctx, buffer, hlsl_ir_loop(instr));
                break;

            case HLSL_IR_STORE:
                write_sm4_store(ctx, buffer, hlsl_ir_store(instr));
                break;

            case HLSL_IR_SWIZZLE:
                write_sm4_swizzle(ctx, buffer, hlsl_ir_swizzle(instr));
                break;

            default:
                hlsl_fixme(ctx, &instr->loc, "Instruction type %s.", hlsl_node_type_to_string(instr->type));
        }
    }
}

static void write_sm4_shdr(struct hlsl_ctx *ctx,
        const struct hlsl_ir_function_decl *entry_func, struct dxbc_writer *dxbc)
{
    const struct hlsl_profile_info *profile = ctx->profile;
    const struct hlsl_ir_var **extern_resources;
    struct vkd3d_bytecode_buffer buffer = {0};
    unsigned int extern_resources_count, i;
    const struct hlsl_buffer *cbuffer;
    const struct hlsl_ir_var *var;
    size_t token_count_position;

    static const uint16_t shader_types[VKD3D_SHADER_TYPE_COUNT] =
    {
        VKD3D_SM4_PS,
        VKD3D_SM4_VS,
        VKD3D_SM4_GS,
        VKD3D_SM5_HS,
        VKD3D_SM5_DS,
        VKD3D_SM5_CS,
        0, /* EFFECT */
        0, /* TEXTURE */
        VKD3D_SM4_LIB,
    };

    extern_resources = sm4_get_extern_resources(ctx, &extern_resources_count);

    put_u32(&buffer, vkd3d_make_u32((profile->major_version << 4) | profile->minor_version, shader_types[profile->type]));
    token_count_position = put_u32(&buffer, 0);

    LIST_FOR_EACH_ENTRY(cbuffer, &ctx->buffers, struct hlsl_buffer, entry)
    {
        if (cbuffer->reg.allocated)
            write_sm4_dcl_constant_buffer(&buffer, cbuffer);
    }

    for (i = 0; i < extern_resources_count; ++i)
    {
        enum hlsl_regset regset;

        var = extern_resources[i];
        regset = hlsl_type_get_regset(var->data_type);

        if (regset == HLSL_REGSET_SAMPLERS)
            write_sm4_dcl_samplers(&buffer, var);
        else if (regset == HLSL_REGSET_TEXTURES)
            write_sm4_dcl_textures(ctx, &buffer, var, false);
        else if (regset == HLSL_REGSET_UAVS)
            write_sm4_dcl_textures(ctx, &buffer, var, true);
    }

    LIST_FOR_EACH_ENTRY(var, &ctx->extern_vars, struct hlsl_ir_var, extern_entry)
    {
        if ((var->is_input_semantic && var->last_read) || (var->is_output_semantic && var->first_write))
            write_sm4_dcl_semantic(ctx, &buffer, var);
    }

    if (profile->type == VKD3D_SHADER_TYPE_COMPUTE)
        write_sm4_dcl_thread_group(&buffer, ctx->thread_count);

    if (ctx->temp_count)
        write_sm4_dcl_temps(&buffer, ctx->temp_count);

    write_sm4_block(ctx, &buffer, &entry_func->body);

    write_sm4_ret(&buffer);

    set_u32(&buffer, token_count_position, bytecode_get_size(&buffer) / sizeof(uint32_t));

    add_section(dxbc, TAG_SHDR, &buffer);

    vkd3d_free(extern_resources);
}

int hlsl_sm4_write(struct hlsl_ctx *ctx, struct hlsl_ir_function_decl *entry_func, struct vkd3d_shader_code *out)
{
    struct dxbc_writer dxbc;
    size_t i;
    int ret;

    dxbc_writer_init(&dxbc);

    write_sm4_signature(ctx, &dxbc, false);
    write_sm4_signature(ctx, &dxbc, true);
    write_sm4_rdef(ctx, &dxbc);
    write_sm4_shdr(ctx, entry_func, &dxbc);

    if (!(ret = ctx->result))
        ret = dxbc_writer_write(&dxbc, out);
    for (i = 0; i < dxbc.section_count; ++i)
        vkd3d_shader_free_shader_code(&dxbc.sections[i].data);
    return ret;
}
