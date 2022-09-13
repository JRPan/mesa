/**************************************************************************
 * 
 * Copyright 2007-2008 VMware, Inc.
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/

#include <inttypes.h>

#include "util/u_debug.h"
#include "util/u_string.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/u_math.h"
#include "tgsi_dump.h"
#include "tgsi_info.h"
#include "tgsi_iterate.h"
#include "tgsi_strings.h"
#include "tgsi_exec.h"
#include "addons/uthash.h"
#include "GL/glcorearb.h"


/** Number of spaces to indent for IF/LOOP/etc */
static const int indent_spaces = 3;


struct dump_ctx
{
   struct tgsi_iterate_context iter;

   boolean dump_float_as_hex;

   uint instno;
   uint immno;
   int indent;
   
   uint indentation;
   FILE *file;

   void (*dump_printf)(struct dump_ctx *ctx, const char *format, ...);
};

static void 
dump_ctx_printf(struct dump_ctx *ctx, const char *format, ...)
{
   va_list ap;
   (void)ctx;
   va_start(ap, format);
   if (ctx->file)
      vfprintf(ctx->file, format, ap);
   else
      _debug_vprintf(format, ap);
   va_end(ap);
}

static void
dump_enum(
   struct dump_ctx *ctx,
   uint e,
   const char **enums,
   uint enum_count )
{
   if (e >= enum_count)
      ctx->dump_printf( ctx, "%u", e );
   else
      ctx->dump_printf( ctx, "%s", enums[e] );
}

#define EOL()           ctx->dump_printf( ctx, "\n" )
#define TXT(S)          ctx->dump_printf( ctx, "%s", S )
#define CHR(C)          ctx->dump_printf( ctx, "%c", C )
#define UIX(I)          ctx->dump_printf( ctx, "0x%x", I )
#define UID(I)          ctx->dump_printf( ctx, "%u", I )
#define SI64D(I)        ctx->dump_printf( ctx, "%"PRId64, I )
#define UI64D(I)        ctx->dump_printf( ctx, "%"PRIu64, I )
#define INSTID(I)       ctx->dump_printf( ctx, "% 3u", I )
#define SID(I)          ctx->dump_printf( ctx, "%d", I )
#define FLT(F)          ctx->dump_printf( ctx, "%10.4f", F )
#define DBL(D)          ctx->dump_printf( ctx, "%10.8f", D )
#define HFLT(F)         ctx->dump_printf( ctx, "0x%08x", fui((F)) )
#define ENM(E,ENUMS)    dump_enum( ctx, E, ENUMS, sizeof( ENUMS ) / sizeof( *ENUMS ) )

const char *
tgsi_swizzle_names[4] =
{
   "x",
   "y",
   "z",
   "w"
};

static void
_dump_register_src(
   struct dump_ctx *ctx,
   const struct tgsi_full_src_register *src )
{
   TXT(tgsi_file_name(src->Register.File));
   if (src->Register.Dimension) {
      if (src->Dimension.Indirect) {
         CHR( '[' );
         TXT(tgsi_file_name(src->DimIndirect.File));
         CHR( '[' );
         SID( src->DimIndirect.Index );
         TXT( "]." );
         ENM( src->DimIndirect.Swizzle, tgsi_swizzle_names );
         if (src->Dimension.Index != 0) {
            if (src->Dimension.Index > 0)
               CHR( '+' );
            SID( src->Dimension.Index );
         }
         CHR( ']' );
         if (src->DimIndirect.ArrayID) {
            CHR( '(' );
            SID( src->DimIndirect.ArrayID );
            CHR( ')' );
         }
      } else {
         CHR('[');
         SID(src->Dimension.Index);
         CHR(']');
      }
   }
   if (src->Register.Indirect) {
      CHR( '[' );
      TXT(tgsi_file_name(src->Indirect.File));
      CHR( '[' );
      SID( src->Indirect.Index );
      TXT( "]." );
      ENM( src->Indirect.Swizzle, tgsi_swizzle_names );
      if (src->Register.Index != 0) {
         if (src->Register.Index > 0)
            CHR( '+' );
         SID( src->Register.Index );
      }
      CHR( ']' );
      if (src->Indirect.ArrayID) {
         CHR( '(' );
         SID( src->Indirect.ArrayID );
         CHR( ')' );
      }
   } else {
      CHR( '[' );
      SID( src->Register.Index );
      CHR( ']' );
   }
}


static void
_dump_register_dst(
   struct dump_ctx *ctx,
   const struct tgsi_full_dst_register *dst )
{
   TXT(tgsi_file_name(dst->Register.File));
   if (dst->Register.Dimension) {
      if (dst->Dimension.Indirect) {
         CHR( '[' );
         TXT(tgsi_file_name(dst->DimIndirect.File));
         CHR( '[' );
         SID( dst->DimIndirect.Index );
         TXT( "]." );
         ENM( dst->DimIndirect.Swizzle, tgsi_swizzle_names );
         if (dst->Dimension.Index != 0) {
            if (dst->Dimension.Index > 0)
               CHR( '+' );
            SID( dst->Dimension.Index );
         }
         CHR( ']' );
         if (dst->DimIndirect.ArrayID) {
            CHR( '(' );
            SID( dst->DimIndirect.ArrayID );
            CHR( ')' );
         }
      } else {
         CHR('[');
         SID(dst->Dimension.Index);
         CHR(']');
      }
   }
   if (dst->Register.Indirect) {
      CHR( '[' );
      TXT(tgsi_file_name(dst->Indirect.File));
      CHR( '[' );
      SID( dst->Indirect.Index );
      TXT( "]." );
      ENM( dst->Indirect.Swizzle, tgsi_swizzle_names );
      if (dst->Register.Index != 0) {
         if (dst->Register.Index > 0)
            CHR( '+' );
         SID( dst->Register.Index );
      }
      CHR( ']' );
      if (dst->Indirect.ArrayID) {
         CHR( '(' );
         SID( dst->Indirect.ArrayID );
         CHR( ')' );
      }
   } else {
      CHR( '[' );
      SID( dst->Register.Index );
      CHR( ']' );
   }
}
static void
_dump_writemask(
   struct dump_ctx *ctx,
   uint writemask )
{
   if (writemask != TGSI_WRITEMASK_XYZW) {
      CHR( '.' );
      if (writemask & TGSI_WRITEMASK_X)
         CHR( 'x' );
      if (writemask & TGSI_WRITEMASK_Y)
         CHR( 'y' );
      if (writemask & TGSI_WRITEMASK_Z)
         CHR( 'z' );
      if (writemask & TGSI_WRITEMASK_W)
         CHR( 'w' );
   }
}

static void
dump_imm_data(struct tgsi_iterate_context *iter,
              union tgsi_immediate_data *data,
              unsigned num_tokens,
              unsigned data_type)
{
   struct dump_ctx *ctx = (struct dump_ctx *)iter;
   unsigned i ;

   TXT( " {" );

   assert( num_tokens <= 4 );
   for (i = 0; i < num_tokens; i++) {
      switch (data_type) {
      case TGSI_IMM_FLOAT64: {
         union di d;
         d.ui = data[i].Uint | (uint64_t)data[i+1].Uint << 32;
         DBL( d.d );
         i++;
         break;
      }
      case TGSI_IMM_INT64: {
         union di d;
         d.i = data[i].Uint | (uint64_t)data[i+1].Uint << 32;
         SI64D( d.i );
         i++;
         break;
      }
      case TGSI_IMM_UINT64: {
         union di d;
         d.ui = data[i].Uint | (uint64_t)data[i+1].Uint << 32;
         UI64D( d.ui );
         i++;
         break;
      }
      case TGSI_IMM_FLOAT32:
         if (ctx->dump_float_as_hex)
            HFLT( data[i].Float );
         else
            FLT( data[i].Float );
         break;
      case TGSI_IMM_UINT32:
         UID(data[i].Uint);
         break;
      case TGSI_IMM_INT32:
         SID(data[i].Int);
         break;
      default:
         assert( 0 );
      }

      if (i < num_tokens - 1)
         TXT( ", " );
   }
   TXT( "}" );
}

static boolean
iter_declaration(
   struct tgsi_iterate_context *iter,
   struct tgsi_full_declaration *decl )
{
   struct dump_ctx *ctx = (struct dump_ctx *)iter;
   boolean patch = decl->Semantic.Name == TGSI_SEMANTIC_PATCH ||
      decl->Semantic.Name == TGSI_SEMANTIC_TESSINNER ||
      decl->Semantic.Name == TGSI_SEMANTIC_TESSOUTER ||
      decl->Semantic.Name == TGSI_SEMANTIC_PRIMID;

   TXT( "DCL " );

   TXT(tgsi_file_name(decl->Declaration.File));

   /* all geometry shader inputs and non-patch tessellation shader inputs are
    * two dimensional
    */
   if (decl->Declaration.File == TGSI_FILE_INPUT &&
       (iter->processor.Processor == PIPE_SHADER_GEOMETRY ||
        (!patch &&
         (iter->processor.Processor == PIPE_SHADER_TESS_CTRL ||
          iter->processor.Processor == PIPE_SHADER_TESS_EVAL)))) {
      TXT("[]");
   }

   /* all non-patch tess ctrl shader outputs are two dimensional */
   if (decl->Declaration.File == TGSI_FILE_OUTPUT &&
       !patch &&
       iter->processor.Processor == PIPE_SHADER_TESS_CTRL) {
      TXT("[]");
   }

   if (decl->Declaration.Dimension) {
      CHR('[');
      SID(decl->Dim.Index2D);
      CHR(']');
   }

   CHR('[');
   SID(decl->Range.First);
   if (decl->Range.First != decl->Range.Last) {
      TXT("..");
      SID(decl->Range.Last);
   }
   CHR(']');

   _dump_writemask(
      ctx,
      decl->Declaration.UsageMask );

   if (decl->Declaration.Array) {
      TXT( ", ARRAY(" );
      SID(decl->Array.ArrayID);
      CHR(')');
   }

   if (decl->Declaration.Local)
      TXT( ", LOCAL" );

   if (decl->Declaration.Semantic) {
      TXT( ", " );
      ENM( decl->Semantic.Name, tgsi_semantic_names );
      if (decl->Semantic.Index != 0 ||
          decl->Semantic.Name == TGSI_SEMANTIC_TEXCOORD ||
          decl->Semantic.Name == TGSI_SEMANTIC_GENERIC) {
         CHR( '[' );
         UID( decl->Semantic.Index );
         CHR( ']' );
      }

      if (decl->Semantic.StreamX != 0 || decl->Semantic.StreamY != 0 ||
          decl->Semantic.StreamZ != 0 || decl->Semantic.StreamW != 0) {
         TXT(", STREAM(");
         UID(decl->Semantic.StreamX);
         TXT(", ");
         UID(decl->Semantic.StreamY);
         TXT(", ");
         UID(decl->Semantic.StreamZ);
         TXT(", ");
         UID(decl->Semantic.StreamW);
         CHR(')');
      }
   }

   if (decl->Declaration.File == TGSI_FILE_IMAGE) {
      TXT(", ");
      ENM(decl->Image.Resource, tgsi_texture_names);
      TXT(", ");
      TXT(util_format_name(decl->Image.Format));
      if (decl->Image.Writable)
         TXT(", WR");
      if (decl->Image.Raw)
         TXT(", RAW");
   }

   if (decl->Declaration.File == TGSI_FILE_BUFFER) {
      if (decl->Declaration.Atomic)
         TXT(", ATOMIC");
   }

   if (decl->Declaration.File == TGSI_FILE_MEMORY) {
      switch (decl->Declaration.MemType) {
      /* Note: ,GLOBAL is optional / the default */
      case TGSI_MEMORY_TYPE_GLOBAL:  TXT(", GLOBAL");  break;
      case TGSI_MEMORY_TYPE_SHARED:  TXT(", SHARED");  break;
      case TGSI_MEMORY_TYPE_PRIVATE: TXT(", PRIVATE"); break;
      case TGSI_MEMORY_TYPE_INPUT:   TXT(", INPUT");   break;
      }
   }

   if (decl->Declaration.File == TGSI_FILE_SAMPLER_VIEW) {
      TXT(", ");
      ENM(decl->SamplerView.Resource, tgsi_texture_names);
      TXT(", ");
      if ((decl->SamplerView.ReturnTypeX == decl->SamplerView.ReturnTypeY) &&
          (decl->SamplerView.ReturnTypeX == decl->SamplerView.ReturnTypeZ) &&
          (decl->SamplerView.ReturnTypeX == decl->SamplerView.ReturnTypeW)) {
         ENM(decl->SamplerView.ReturnTypeX, tgsi_return_type_names);
      } else {
         ENM(decl->SamplerView.ReturnTypeX, tgsi_return_type_names);
         TXT(", ");
         ENM(decl->SamplerView.ReturnTypeY, tgsi_return_type_names);
         TXT(", ");
         ENM(decl->SamplerView.ReturnTypeZ, tgsi_return_type_names);
         TXT(", ");
         ENM(decl->SamplerView.ReturnTypeW, tgsi_return_type_names);
      }
   }

   if (decl->Declaration.Interpolate) {
      if (iter->processor.Processor == PIPE_SHADER_FRAGMENT &&
          decl->Declaration.File == TGSI_FILE_INPUT)
      {
         TXT( ", " );
         ENM( decl->Interp.Interpolate, tgsi_interpolate_names );
      }

      if (decl->Interp.Location != TGSI_INTERPOLATE_LOC_CENTER) {
         TXT( ", " );
         ENM( decl->Interp.Location, tgsi_interpolate_locations );
      }

      if (decl->Interp.CylindricalWrap) {
         TXT(", CYLWRAP_");
         if (decl->Interp.CylindricalWrap & TGSI_CYLINDRICAL_WRAP_X) {
            CHR('X');
         }
         if (decl->Interp.CylindricalWrap & TGSI_CYLINDRICAL_WRAP_Y) {
            CHR('Y');
         }
         if (decl->Interp.CylindricalWrap & TGSI_CYLINDRICAL_WRAP_Z) {
            CHR('Z');
         }
         if (decl->Interp.CylindricalWrap & TGSI_CYLINDRICAL_WRAP_W) {
            CHR('W');
         }
      }
   }

   if (decl->Declaration.Invariant) {
      TXT( ", INVARIANT" );
   }

   EOL();

   return TRUE;
}

void
tgsi_dump_declaration(
   const struct tgsi_full_declaration *decl )
{
   struct dump_ctx ctx;
   memset(&ctx, 0, sizeof(ctx));

   ctx.dump_printf = dump_ctx_printf;

   iter_declaration( &ctx.iter, (struct tgsi_full_declaration *)decl );
}

static boolean
iter_property(
   struct tgsi_iterate_context *iter,
   struct tgsi_full_property *prop )
{
   unsigned i;
   struct dump_ctx *ctx = (struct dump_ctx *)iter;

   TXT( "PROPERTY " );
   ENM(prop->Property.PropertyName, tgsi_property_names);

   if (prop->Property.NrTokens > 1)
      TXT(" ");

   for (i = 0; i < prop->Property.NrTokens - 1; ++i) {
      switch (prop->Property.PropertyName) {
      case TGSI_PROPERTY_GS_INPUT_PRIM:
      case TGSI_PROPERTY_GS_OUTPUT_PRIM:
         ENM(prop->u[i].Data, tgsi_primitive_names);
         break;
      case TGSI_PROPERTY_FS_COORD_ORIGIN:
         ENM(prop->u[i].Data, tgsi_fs_coord_origin_names);
         break;
      case TGSI_PROPERTY_FS_COORD_PIXEL_CENTER:
         ENM(prop->u[i].Data, tgsi_fs_coord_pixel_center_names);
         break;
      case TGSI_PROPERTY_NEXT_SHADER:
         ENM(prop->u[i].Data, tgsi_processor_type_names);
         break;
      default:
         SID( prop->u[i].Data );
         break;
      }
      if (i < prop->Property.NrTokens - 2)
         TXT( ", " );
   }
   EOL();

   return TRUE;
}

void tgsi_dump_property(
   const struct tgsi_full_property *prop )
{
   struct dump_ctx ctx;
   memset(&ctx, 0, sizeof(ctx));

   ctx.dump_printf = dump_ctx_printf;

   iter_property( &ctx.iter, (struct tgsi_full_property *)prop );
}

static boolean
iter_immediate(
   struct tgsi_iterate_context *iter,
   struct tgsi_full_immediate *imm )
{
   struct dump_ctx *ctx = (struct dump_ctx *) iter;

   TXT( "IMM[" );
   SID( ctx->immno++ );
   TXT( "] " );
   ENM( imm->Immediate.DataType, tgsi_immediate_type_names );

   dump_imm_data(iter, imm->u, imm->Immediate.NrTokens - 1,
                 imm->Immediate.DataType);

   EOL();

   return TRUE;
}

void
tgsi_dump_immediate(
   const struct tgsi_full_immediate *imm )
{
   struct dump_ctx ctx;
   memset(&ctx, 0, sizeof(ctx));

   ctx.dump_printf = dump_ctx_printf;

   iter_immediate( &ctx.iter, (struct tgsi_full_immediate *)imm );
}

static boolean
iter_instruction(
   struct tgsi_iterate_context *iter,
   struct tgsi_full_instruction *inst )
{
   struct dump_ctx *ctx = (struct dump_ctx *) iter;
   uint instno = ctx->instno++;
   const struct tgsi_opcode_info *info = tgsi_get_opcode_info( inst->Instruction.Opcode );
   uint i;
   boolean first_reg = TRUE;

   INSTID( instno );
   TXT( ": " );

   ctx->indent -= info->pre_dedent;
   for(i = 0; (int)i < ctx->indent; ++i)
      TXT( "  " );
   ctx->indent += info->post_indent;

   TXT( tgsi_get_opcode_name(inst->Instruction.Opcode) );

   if (inst->Instruction.Saturate) {
      TXT( "_SAT" );
   }

   if (inst->Instruction.Precise) {
      TXT( "_PRECISE" );
   }

   for (i = 0; i < inst->Instruction.NumDstRegs; i++) {
      const struct tgsi_full_dst_register *dst = &inst->Dst[i];

      if (!first_reg)
         CHR( ',' );
      CHR( ' ' );

      _dump_register_dst( ctx, dst );
      _dump_writemask( ctx, dst->Register.WriteMask );

      first_reg = FALSE;
   }

   for (i = 0; i < inst->Instruction.NumSrcRegs; i++) {
      const struct tgsi_full_src_register *src = &inst->Src[i];

      if (!first_reg)
         CHR( ',' );
      CHR( ' ' );

      if (src->Register.Negate)
         CHR( '-' );
      if (src->Register.Absolute)
         CHR( '|' );

      _dump_register_src(ctx, src);

      if (src->Register.SwizzleX != TGSI_SWIZZLE_X ||
          src->Register.SwizzleY != TGSI_SWIZZLE_Y ||
          src->Register.SwizzleZ != TGSI_SWIZZLE_Z ||
          src->Register.SwizzleW != TGSI_SWIZZLE_W) {
         CHR( '.' );
         ENM( src->Register.SwizzleX, tgsi_swizzle_names );
         ENM( src->Register.SwizzleY, tgsi_swizzle_names );
         ENM( src->Register.SwizzleZ, tgsi_swizzle_names );
         ENM( src->Register.SwizzleW, tgsi_swizzle_names );
      }

      if (src->Register.Absolute)
         CHR( '|' );

      first_reg = FALSE;
   }

   if (inst->Instruction.Texture) {
      if (!(inst->Instruction.Opcode >= TGSI_OPCODE_SAMPLE &&
            inst->Instruction.Opcode <= TGSI_OPCODE_GATHER4)) {
         TXT( ", " );
         ENM( inst->Texture.Texture, tgsi_texture_names );
      }
      for (i = 0; i < inst->Texture.NumOffsets; i++) {
         TXT( ", " );
         TXT(tgsi_file_name(inst->TexOffsets[i].File));
         CHR( '[' );
         SID( inst->TexOffsets[i].Index );
         CHR( ']' );
         CHR( '.' );
         ENM( inst->TexOffsets[i].SwizzleX, tgsi_swizzle_names);
         ENM( inst->TexOffsets[i].SwizzleY, tgsi_swizzle_names);
         ENM( inst->TexOffsets[i].SwizzleZ, tgsi_swizzle_names);
      }
   }

   if (inst->Instruction.Memory) {
      uint32_t qualifier = inst->Memory.Qualifier;
      while (qualifier) {
         int bit = ffs(qualifier) - 1;
         qualifier &= ~(1U << bit);
         TXT(", ");
         ENM(bit, tgsi_memory_names);
      }
      if (inst->Memory.Texture) {
         TXT( ", " );
         ENM( inst->Memory.Texture, tgsi_texture_names );
      }
      if (inst->Memory.Format) {
         TXT( ", " );
         TXT( util_format_name(inst->Memory.Format) );
      }
   }

   if (inst->Instruction.Label) {
      switch (inst->Instruction.Opcode) {
      case TGSI_OPCODE_IF:
      case TGSI_OPCODE_UIF:
      case TGSI_OPCODE_ELSE:
      case TGSI_OPCODE_BGNLOOP:
      case TGSI_OPCODE_ENDLOOP:
      case TGSI_OPCODE_CAL:
      case TGSI_OPCODE_BGNSUB:
         TXT( " :" );
         UID( inst->Label.Label );
         break;
      }
   }

   /* update indentation */
   if (inst->Instruction.Opcode == TGSI_OPCODE_IF ||
       inst->Instruction.Opcode == TGSI_OPCODE_UIF ||
       inst->Instruction.Opcode == TGSI_OPCODE_ELSE ||
       inst->Instruction.Opcode == TGSI_OPCODE_BGNLOOP) {
      ctx->indentation += indent_spaces;
   }

   EOL();

   return TRUE;
}

static boolean
gen_ptx_instruction(
//iter_instruction(
   int shader_type,
   FILE* inst_stream,
   struct tgsi_full_instruction *inst );


void
tgsi_dump_instruction(
   const struct tgsi_full_instruction *inst,
   uint instno )
{
   struct dump_ctx ctx;
   memset(&ctx, 0, sizeof(ctx));

   ctx.instno = instno;
   ctx.immno = instno;
   ctx.indent = 0;
   ctx.dump_printf = dump_ctx_printf;
   ctx.indentation = 0;
   ctx.file = NULL;

   iter_instruction( &ctx.iter, (struct tgsi_full_instruction *)inst );

   //struct dump_ctx *dctx = (struct dump_ctx *) &ctx;
   //gen_ptx_instruction( dctx->instno, (struct tgsi_full_instruction *)inst);
}

static boolean
prolog(
   struct tgsi_iterate_context *iter )
{
   struct dump_ctx *ctx = (struct dump_ctx *) iter;
   ENM( iter->processor.Processor, tgsi_processor_type_names );
   EOL();
   return TRUE;
}

static void
init_dump_ctx(struct dump_ctx *ctx, uint flags)
{
   memset(ctx, 0, sizeof(*ctx));

   ctx->iter.prolog = prolog;
   ctx->iter.iterate_instruction = iter_instruction;
   ctx->iter.iterate_declaration = iter_declaration;
   ctx->iter.iterate_immediate = iter_immediate;
   ctx->iter.iterate_property = iter_property;

   if (flags & TGSI_DUMP_FLOAT_AS_HEX)
      ctx->dump_float_as_hex = TRUE;
}

void
tgsi_dump_to_file(const struct tgsi_token *tokens, uint flags, FILE *file)
{
   struct dump_ctx ctx;
   memset(&ctx, 0, sizeof(ctx));

   init_dump_ctx(&ctx, flags);

   ctx.dump_printf = dump_ctx_printf;
   ctx.file = file;

   tgsi_iterate_shader( tokens, &ctx.iter );
}

void
tgsi_dump(const struct tgsi_token *tokens, uint flags)
{
   tgsi_dump_to_file(tokens, flags, NULL);
}

struct str_dump_ctx
{
   struct dump_ctx base;
   char *str;
   char *ptr;
   int left;
   bool nospace;
};

static void
str_dump_ctx_printf(struct dump_ctx *ctx, const char *format, ...)
{
   struct str_dump_ctx *sctx = (struct str_dump_ctx *)ctx;
   
   if (!sctx->nospace) {
      int written;
      va_list ap;
      va_start(ap, format);
      written = util_vsnprintf(sctx->ptr, sctx->left, format, ap);
      va_end(ap);

      /* Some complicated logic needed to handle the return value of
       * vsnprintf:
       */
      if (written > 0) {
         if (written >= sctx->left) {
            sctx->nospace = true;
            written = sctx->left;
         }
         sctx->ptr += written;
         sctx->left -= written;
      }
   }
}

bool
tgsi_dump_str(
   const struct tgsi_token *tokens,
   uint flags,
   char *str,
   size_t size)
{
   struct str_dump_ctx ctx;
   memset(&ctx, 0, sizeof(ctx));

   init_dump_ctx(&ctx.base, flags);

   ctx.base.dump_printf = &str_dump_ctx_printf;

   ctx.str = str;
   ctx.str[0] = 0;
   ctx.ptr = str;
   ctx.left = (int)size;
   ctx.nospace = false;

   tgsi_iterate_shader( tokens, &ctx.base.iter );

   return !ctx.nospace;
}

void
tgsi_dump_instruction_str(
   const struct tgsi_full_instruction *inst,
   uint instno,
   char *str,
   size_t size)
{
   struct str_dump_ctx ctx;
   memset(&ctx, 0, sizeof(ctx));

   ctx.base.instno = instno;
   ctx.base.immno = instno;
   ctx.base.indent = 0;
   ctx.base.dump_printf = &str_dump_ctx_printf;
   ctx.base.indentation = 0;
   ctx.base.file = NULL;

   ctx.str = str;
   ctx.str[0] = 0;
   ctx.ptr = str;
   ctx.left = (int)size;
   ctx.nospace = false;

   iter_instruction( &ctx.base.iter, (struct tgsi_full_instruction *)inst );
}


/////////////////////////////////////////////////////////////////
//  generating ptx instructions
/////////////////////////////////////////////////////////////////
//

/*const char *tgsi_processor_type_names[6] =
{
   "VERT",
   "FRAG",
   "GEOM",
   "TESS_CTRL",
   "TESS_EVAL",
   "COMP"
};

static const char *tgsi_file_names[] =
{
   "NULL",
   "CONST",
   "IN",
   "OUT",
   "TEMP",
   "SAMP",
   "ADDR",
   "IMM",
   "SV",
   "IMAGE",
   "SVIEW",
   "BUFFER",
   "MEMORY",
};

const char *tgsi_semantic_names[TGSI_SEMANTIC_COUNT] =
{
   "POSITION",
   "COLOR",
   "BCOLOR",
   "FOG",
   "PSIZE",
   "GENERIC",
   "NORMAL",
   "FACE",
   "EDGEFLAG",
   "PRIM_ID",
   "INSTANCEID",
   "VERTEXID",
   "STENCIL",
   "CLIPDIST",
   "CLIPVERTEX",
   "GRID_SIZE",
   "BLOCK_ID",
   "BLOCK_SIZE",
   "THREAD_ID",
   "TEXCOORD",
   "PCOORD",
   "VIEWPORT_INDEX",
   "LAYER",
   "SAMPLEID",
   "SAMPLEPOS",
   "SAMPLEMASK",
   "INVOCATIONID",
   "VERTEXID_NOBASE",
   "BASEVERTEX",
   "PATCH",
   "TESSCOORD",
   "TESSOUTER",
   "TESSINNER",
   "VERTICESIN",
   "HELPER_INVOCATION",
   "BASEINSTANCE",
   "DRAWID",
   "WORK_DIM",
   "SUBGROUP_SIZE",
   "SUBGROUP_INVOCATION",
   "SUBGROUP_EQ_MASK",
   "SUBGROUP_GE_MASK",
   "SUBGROUP_GT_MASK",
   "SUBGROUP_LE_MASK",
   "SUBGROUP_LT_MASK",
};*/

const char *tgsi_texture_names_ptx[TGSI_TEXTURE_COUNT] =
{
   "BUFFER",
   "1D",
   "2d",
   "3D",
   "CUBE",
   "RECT",
   "SHADOW1D",
   "SHADOW2D",
   "SHADOWRECT",
   "1D_ARRAY",
   "2D_ARRAY",
   "SHADOW1D_ARRAY",
   "SHADOW2D_ARRAY",
   "SHADOWCUBE",
   "2D_MSAA",
   "2D_ARRAY_MSAA",
   "CUBEARRAY",
   "SHADOWCUBEARRAY",
   "UNKNOWN",
};

const int tgsi_texture_names_coords[TGSI_TEXTURE_COUNT] =
{
   -1, //"BUFFER",
   -1, //"1D",
   2, //"2d",
   -1, //"3D",
   -1, //"CUBE",
   -1, //"RECT",
   -1, //"SHADOW1D",
   -1, //"SHADOW2D",
   -1, //"SHADOWRECT",
   -1, //"1D_ARRAY",
   -1, //"2D_ARRAY",
   -1, //"SHADOW1D_ARRAY",
   -1, //"SHADOW2D_ARRAY",
   -1, //"SHADOWCUBE",
   -1, //"2D_MSAA",
   -1, //"2D_ARRAY_MSAA",
   -1, //"CUBEARRAY",
   -1, //"SHADOWCUBEARRAY",
   -1, //"UNKNOWN",
};



/*const char *tgsi_property_names[TGSI_PROPERTY_COUNT] =
{
   "GS_INPUT_PRIMITIVE",
   "GS_OUTPUT_PRIMITIVE",
   "GS_MAX_OUTPUT_VERTICES",
   "FS_COORD_ORIGIN",
   "FS_COORD_PIXEL_CENTER",
   "FS_COLOR0_WRITES_ALL_CBUFS",
   "FS_DEPTH_LAYOUT",
   "VS_PROHIBIT_UCPS",
   "GS_INVOCATIONS",
   "VS_WINDOW_SPACE_POSITION",
   "TCS_VERTICES_OUT",
   "TES_PRIM_MODE",
   "TES_SPACING",
   "TES_VERTEX_ORDER_CW",
   "TES_POINT_MODE",
   "NUM_CLIPDIST_ENABLED",
   "NUM_CULLDIST_ENABLED",
   "FS_EARLY_DEPTH_STENCIL",
   "FS_POST_DEPTH_COVERAGE",
   "NEXT_SHADER",
   "CS_FIXED_BLOCK_WIDTH",
   "CS_FIXED_BLOCK_HEIGHT",
   "CS_FIXED_BLOCK_DEPTH",
   "MUL_ZERO_WINS",
};

const char *tgsi_return_type_names[TGSI_RETURN_TYPE_COUNT] =
{
   "UNORM",
   "SNORM",
   "SINT",
   "UINT",
   "FLOAT"
};

const char *tgsi_interpolate_names[TGSI_INTERPOLATE_COUNT] =
{
   "CONSTANT",
   "LINEAR",
   "PERSPECTIVE",
   "COLOR"
};

const char *tgsi_interpolate_locations[TGSI_INTERPOLATE_LOC_COUNT] =
{
   "CENTER",
   "CENTROID",
   "SAMPLE",
};

const char *tgsi_primitive_names[PIPE_PRIM_MAX] =
{
   "POINTS",
   "LINES",
   "LINE_LOOP",
   "LINE_STRIP",
   "TRIANGLES",
   "TRIANGLE_STRIP",
   "TRIANGLE_FAN",
   "QUADS",
   "QUAD_STRIP",
   "POLYGON",
   "LINES_ADJACENCY",
   "LINE_STRIP_ADJACENCY",
   "TRIANGLES_ADJACENCY",
   "TRIANGLE_STRIP_ADJACENCY",
   "PATCHES",
};

const char *tgsi_fs_coord_origin_names[2] =
{
   "UPPER_LEFT",
   "LOWER_LEFT"
};

const char *tgsi_fs_coord_pixel_center_names[2] =
{
   "HALF_INTEGER",
   "INTEGER"
};*/

const char *tgsi_immediate_type_names_ptx[6] =
{
   "f32",
   "u32",
   "s32",
   "f64",
   "u64",
   "s64",
};

/*
const char *tgsi_memory_names[3] =
{
   "COHERENT",
   "RESTRICT",
   "VOLATILE",
};
*/


extern void gpgpusimSetShaderRegs(int shader_type, int usedRegs);
extern void gpgpusimSetShaderCode(int shader_type, int usedRegs);

typedef struct shader_stats_type {
  int usedRegs;
} shader_stats_t;

typedef struct ptx_opcode_type {
  const char* const name;
  int enabled;
  int num_dst;
  int num_src;
  const char* const ptx_type;
  bool is_tex;
} ptx_opcode_t;

typedef struct register_dic_type {
    char* reg_key;
    char* reg_value;
    UT_hash_handle hh;
} register_dic_t;

register_dic_t* register_dictionary;

#define OPCODE(_num_dst, _num_src, _output_mode, name, enabled, ptx_type, is_tex, ...) {#name, enabled, _num_dst,  _num_src, #ptx_type, is_tex},
#define OPCODE_GAP(opc) {"UNK", 0},

static const ptx_opcode_t ptx_opcodes [] = //[TGSI_OPCODE_LAST] =
{
#include "tgsi_ptx_opcodes.h"
};

#undef OPCODE
#undef OPCODE_GAP


#undef TXT
#undef CHR
#undef SID
#undef ENM
#undef UID
#define TXT(S)          printf( "%s", S )
#define CHR(C)          printf( "%c", C )
#define SID(I)          printf( "%d", I )
#define UID(I)          printf( "%u", I )
#define ENM(E,ENUMS)    printf( "%s", ENUMS[E])

static char*
get_register_dst_name(int shader_type,
   const struct tgsi_full_dst_register *dst, int swiz, bool* signalDummy)
{
   char* dstRegName[3];
   dstRegName[0] = (char*) tgsi_file_name(dst->Register.File);
   dstRegName[1] = dstRegName [2] = "";

   assert(!dst->Register.Dimension);
   if (dst->Register.Dimension) {
      if (dst->Dimension.Indirect) {
         CHR( '[' );
         TXT(tgsi_file_name(dst->DimIndirect.File));
         CHR( '[' );
         SID( dst->DimIndirect.Index );
         TXT( "]." );
         ENM( dst->DimIndirect.Swizzle, tgsi_swizzle_names );
         if (dst->Dimension.Index != 0) {
            if (dst->Dimension.Index > 0)
               CHR( '+' );
            SID( dst->Dimension.Index );
         }
         CHR( ']' );
         if (dst->DimIndirect.ArrayID) {
            CHR( '(' );
            SID( dst->DimIndirect.ArrayID );
            CHR( ')' );
         }
      } else {
         //CHR('[');
         //SID(dst->Dimension.Index);
         //CHR(']');
         asprintf(&dstRegName[1], "%d", dst->Dimension.Index);
      }
   }

   assert(!dst->Register.Indirect);
   if (dst->Register.Indirect) {
      CHR( '[' );
      TXT(tgsi_file_name(dst->Indirect.File));
      CHR( '[' );
      SID( dst->Indirect.Index );
      TXT( "]." );
      ENM( dst->Indirect.Swizzle, tgsi_swizzle_names );
      if (dst->Register.Index != 0) {
         if (dst->Register.Index > 0)
            CHR( '+' );
         SID( dst->Register.Index );
      }
      CHR( ']' );
      if (dst->Indirect.ArrayID) {
         CHR( '(' );
         SID( dst->Indirect.ArrayID );
         CHR( ')' );
      }
   } else {
      /*CHR( '[' );
      SID( dst->Register.Index );
      CHR( ']' );*/
      asprintf(&dstRegName[2], "%d", dst->Register.Index);
   }


   char* dstRegNameFull;
   asprintf(&dstRegNameFull, "%s%s%s", dstRegName[0], dstRegName[1], dstRegName[2]);


   register_dic_t* dr = NULL;
   HASH_FIND_STR(register_dictionary, dstRegNameFull, dr);

   const char* dst_swizzle = tgsi_swizzle_names[swiz];

   free((void*)dstRegNameFull);


   bool special_reg = false;
   if(!strcmp(dstRegName[0], "OUT")){
     special_reg = true;
   }

   if(special_reg){
     if(dr  && (shader_type == GL_FRAGMENT_SHADER)){
       asprintf(&dstRegNameFull, "%s%s%s.%s", dr->reg_value, dstRegName[1], dstRegName[2], dst_swizzle);
     } else {
       if(shader_type == GL_VERTEX_SHADER){
          //if we're writing to OUT we signal using dummy register, only will be used if the instruction is not mov
          if(signalDummy!=NULL)
             *signalDummy = true;
       }
       asprintf(&dstRegNameFull, "%s%s%s.%s", dstRegName[0], dstRegName[1], dstRegName[2], dst_swizzle);
     }
   } else {
     asprintf(&dstRegNameFull, "%s%s%s%s", dstRegName[0], dstRegName[1], dstRegName[2], dst_swizzle);
   }

   /*if(dr){
     asprintf(&dstRegNameFull, "%s.%s", dr->reg_value, dst_swizzle);
   } else {
     asprintf(&dstRegNameFull, "%s%s%s%s", dstRegName[0], dstRegName[1], dstRegName[2], dst_swizzle);
   }*/

   if(strlen(dstRegName[1]) > 0) free(dstRegName[1]);
   if(strlen(dstRegName[2]) > 0) free(dstRegName[2]);
   return dstRegNameFull;
}

static const char* get_src_swizzle(const struct tgsi_full_src_register* src, int position){
  const char * src_swizzle = src_swizzle = tgsi_swizzle_names[position];
  switch(position){
    case 0:
      if (src->Register.SwizzleX != TGSI_SWIZZLE_X) {
        src_swizzle =  tgsi_swizzle_names[src->Register.SwizzleX];
      }
      break;
    case 1:
      if(src->Register.SwizzleY != TGSI_SWIZZLE_Y) {
        src_swizzle =  tgsi_swizzle_names[src->Register.SwizzleY];
      }
      break;
    case 2:
      if(src->Register.SwizzleZ != TGSI_SWIZZLE_Z){
        src_swizzle =  tgsi_swizzle_names[src->Register.SwizzleZ];
      }
      break;
    case 3:
      if(src->Register.SwizzleW != TGSI_SWIZZLE_W) {
        src_swizzle =  tgsi_swizzle_names[src->Register.SwizzleW];
      }
      break;
    default: assert("undefined position value!" == 0);
  }

  return src_swizzle;
}

static char*
get_register_src_name(int shader_type,
   const struct tgsi_full_src_register *src, int swiz )
{
   char* srcRegName[3];
   srcRegName[0] = (char*) tgsi_file_name(src->Register.File);
   srcRegName[1] = srcRegName [2] = "";

   if (src->Register.Dimension) {
      assert(!src->Dimension.Indirect);
      if (src->Dimension.Indirect) {
         CHR( '[' );
         TXT(tgsi_file_name(src->DimIndirect.File));
         CHR( '[' );
         SID( src->DimIndirect.Index );
         TXT( "]." );
         ENM( src->DimIndirect.Swizzle, tgsi_swizzle_names );
         if (src->Dimension.Index != 0) {
            if (src->Dimension.Index > 0)
               CHR( '+' );
            SID( src->Dimension.Index );
         }
         CHR( ']' );
         if (src->DimIndirect.ArrayID) {
            CHR( '(' );
            SID( src->DimIndirect.ArrayID );
            CHR( ')' );
         }
      } else {
         //CHR('[');
         //SID(src->Dimension.Index);
         //CHR(']');
         asprintf(&srcRegName[1], "%d", src->Dimension.Index);
      }
   }

   assert(!src->Register.Indirect);
   if (src->Register.Indirect) {
      CHR( '[' );
      TXT(tgsi_file_name(src->Indirect.File));
      CHR( '[' );
      SID( src->Indirect.Index );
      TXT( "]." );
      ENM( src->Indirect.Swizzle, tgsi_swizzle_names );
      if (src->Register.Index != 0) {
         if (src->Register.Index > 0)
            CHR( '+' );
         SID( src->Register.Index );
      }
      CHR( ']' );
      if (src->Indirect.ArrayID) {
         CHR( '(' );
         SID( src->Indirect.ArrayID );
         CHR( ')' );
      }
   } else {
      //CHR( '[' );
      //SID( src->Register.Index );
      //CHR( ']' );
      asprintf(&srcRegName[2], "%d", src->Register.Index);
   }

   char* srcRegNameFull;

   const char * src_swizzle = get_src_swizzle(src, swiz);

   asprintf(&srcRegNameFull, "%s%s%s", srcRegName[0], srcRegName[1], srcRegName[2]);

   register_dic_t* rd = NULL;
   HASH_FIND_STR(register_dictionary, srcRegNameFull, rd);
   free((void*) srcRegNameFull);


   bool special_reg = false;
   bool isIN = !strcmp(srcRegName[0], "IN");
   bool isCONST = !strcmp(srcRegName[0], "CONST");
   /*if(!strcmp(srcRegName[0], "IN")
      || !strcmp(srcRegName[0],"CONST")){
     special_reg = true;
   }*/

   if(isIN || isCONST){ 
      if((shader_type == GL_FRAGMENT_SHADER) || isCONST) 
        asprintf(&srcRegNameFull, "%s%s%s.%s", srcRegName[0], srcRegName[1], srcRegName[2], src_swizzle);
      else 
        asprintf(&srcRegNameFull, "%s%s%s%s", srcRegName[0], srcRegName[1], srcRegName[2], src_swizzle);
   } else {
     //asprintf(&srcRegNameFull, "%s%s%s%s", srcRegName[0], srcRegName[1], srcRegName[2], src_swizzle);
     if(rd == NULL){
       asprintf(&srcRegNameFull, "%s%s%s%s", srcRegName[0], srcRegName[1], srcRegName[2], src_swizzle);
       HASH_FIND_STR(register_dictionary, srcRegNameFull, rd);
       if(rd){
         free((void*) srcRegNameFull);
         asprintf(&srcRegNameFull, "%s", rd->reg_value);
       }
     } else {
       asprintf(&srcRegNameFull, "%s.%s", rd->reg_value, src_swizzle);
     }
   }

   if(strlen(srcRegName[1]) > 0) free(srcRegName[1]);
   if(strlen(srcRegName[2]) > 0) free(srcRegName[2]);

   return srcRegNameFull;
}

//iter_instruction(
static boolean
gen_ptx_instruction(
   int shader_type,
   FILE* inst_stream,
   struct tgsi_full_instruction *inst)
{
   uint i, src_i, dst_i;
   //boolean first_reg = TRUE;
   unsigned int opcode = inst->Instruction.Opcode;

   //TXT(ptx_opcode_names[inst->Instruction.Opcode]);
   if(!ptx_opcodes[opcode].enabled){
     printf("TGSI to PTX: instruction %s not supported.\n", ptx_opcodes[opcode].name);
     assert(0);
   }
   if (ptx_opcodes[opcode].name == "UIF" || 
         ptx_opcodes[opcode].name == "ELSE" || 
         ptx_opcodes[opcode].name == "ENDIF")
   {
      printf("WARNING - control flow detected (IF) - checked TGSI\n");
      return TRUE;
   }


   //TODO
   /*if (inst->Instruction.Precise) {
      TXT( "_PRECISE" );
   }*/

   assert(inst->Instruction.NumDstRegs == ptx_opcodes[opcode].num_dst);

   assert(ptx_opcodes[opcode].num_dst <= 1);
   assert(ptx_opcodes[opcode].num_src <= 3);

   if (inst->Instruction.Texture) {
     char* textureType;
     if (!(inst->Instruction.Opcode >= TGSI_OPCODE_SAMPLE &&
           inst->Instruction.Opcode <= TGSI_OPCODE_GATHER4)) {
       textureType = tgsi_texture_names_ptx[inst->Texture.Texture];
     }
     for (i = 0; i < inst->Texture.NumOffsets; i++) {
       assert(0);
       TXT( ", " );
       TXT(tgsi_file_name(inst->TexOffsets[i].File));
       CHR( '[' );
       SID( inst->TexOffsets[i].Index );
       CHR( ']' );
       CHR( '.' );
       ENM( inst->TexOffsets[i].SwizzleX, tgsi_swizzle_names);
       ENM( inst->TexOffsets[i].SwizzleY, tgsi_swizzle_names);
       ENM( inst->TexOffsets[i].SwizzleZ, tgsi_swizzle_names);
     }

     assert(inst->Instruction.NumDstRegs == 1);
     const struct tgsi_full_dst_register *dst = &inst->Dst[0];
     uint writeMask = dst->Register.WriteMask;

     int dstCount = 0;
     while(writeMask){
       dstCount++;
       writeMask >>= 1;
     }

     assert(dstCount >=0 && dstCount <= 4);
   //   fprintf(inst_stream, "%s", ptx_opcodes[opcode].name);
     fprintf(inst_stream, "%s", "tex");
     if(strlen(textureType) > 0)
       fprintf(inst_stream, ".%s", textureType);
     fprintf(inst_stream, ".v2.f32.f32 ", dstCount);

     writeMask = dst->Register.WriteMask;
     int maskBit = -1;
     fprintf(inst_stream, "{ ");
     while(writeMask) {
       maskBit++;
       bool enabled = (1 & writeMask) != 0;
       writeMask >>= 1;
       if(!enabled){
         fprintf(inst_stream, "dummy_%d ", maskBit);
         if(writeMask)
           fprintf(inst_stream, ", ");
         continue;
       }

      // const char* dst_swizzle = tgsi_swizzle_names[maskBit];
       char* dstRegName = (char*)get_register_dst_name(shader_type, dst, maskBit, NULL);

       fprintf(inst_stream, "%s ", dstRegName);
       if(writeMask)
         fprintf(inst_stream, ", ");

     }
     fprintf(inst_stream, " }, ");



     assert(inst->Instruction.NumSrcRegs == 2);
     const struct tgsi_full_src_register * tex_sampler = &inst->Src[1];
     //const char* sampler_src_name = get_register_src_name(tex_sampler);

     assert(!strcmp(tgsi_file_name(tex_sampler->Register.File), "SAMP"));

     /*fprintf(inst_stream, "[textureReference%d_%s, ",
            tex_sampler->Register.Index, tgsi_texture_names[inst->Texture.Texture]);*/

     fprintf(inst_stream, "[TGSI_SAMP_%d, ",
            tex_sampler->Register.Index);

     const struct tgsi_full_src_register * coords_reg = &inst->Src[0];
     int textureDim = tgsi_texture_names_coords[inst->Texture.Texture];
     fprintf(inst_stream, "{ ");
     int sw = 0;
     for(sw =0; sw < textureDim; sw++){
       const char* coords_src_name = get_register_src_name(shader_type, coords_reg, sw);
       //const char* swizzle = get_src_swizzle(coords_reg, sw);
       fprintf(inst_stream, "%s", coords_src_name);
       if(sw != textureDim-1){
         fprintf(inst_stream, ", ");
       } else fprintf(inst_stream, " }");
       free((void*) coords_src_name);
     }


     fprintf(inst_stream, "];\n");
     return TRUE;
   }

   //e.g., exit, kill ..etc
   if(inst->Instruction.NumDstRegs==0 && inst->Instruction.NumSrcRegs==0){
     fprintf(inst_stream, "%s;\n", ptx_opcodes[opcode].name);
     return TRUE;
   }

   //other instruction
   char srcRegNames[5][100];  //5 max num of src registers
   char* ptxInst;
   char* satStr = "";
   char* inst_name = ptx_opcodes[opcode].name;

   if (inst->Instruction.Saturate) {
      //TXT( ".sat" );
      if(ptx_opcodes[opcode].name == "mov"){
        inst_name = "cvt.sat.f32";
      } else {
        satStr = ".sat";
      }
   }
   if (ptx_opcodes[opcode].name == "MIN"){
      inst_name = "min";
   } else if(ptx_opcodes[opcode].name == "FSLT"){
      inst_name = "set.lt";
   } else if(ptx_opcodes[opcode].name == "UCMP"){
      inst_name = "mov";
   } else if(ptx_opcodes[opcode].name == "ISHR"){
      inst_name = "shr";
   } else if(ptx_opcodes[opcode].name == "I2F"){
      inst_name = "cvt";
   } else if(ptx_opcodes[opcode].name == "FLR"){
      inst_name = "cvt";
   } else if(ptx_opcodes[opcode].name == "FRC"){
      inst_name = "cvt";
   }
   
   asprintf(&ptxInst, "%s%s%s", inst_name, satStr, ptx_opcodes[opcode].ptx_type);

   bool dpOp = false;
   if((strcmp(ptx_opcodes[opcode].name, "dp2")==0)
      || (strcmp(ptx_opcodes[opcode].name, "dp3")==0)){
     dpOp = true;
   }

   for (dst_i = 0; dst_i < inst->Instruction.NumDstRegs; dst_i++) {
      const struct tgsi_full_dst_register *dst = &inst->Dst[dst_i];

      uint writeMask = dst->Register.WriteMask;
      int dstCount = 0;
      while(writeMask){
         dstCount++;
         writeMask >>= 1;
      }

      int maskBit = -1;
      bool useDummyDst = false;
      if(dstCount > 1 && !dpOp){
         writeMask = dst->Register.WriteMask;
         while(writeMask) {
            maskBit++;
            bool enabled = (1 & writeMask) != 0;
            writeMask >>= 1;
            if(!enabled) continue;
            int dst_i;
            for(dst_i = 0; dst_i < maskBit; dst_i++){
               for (src_i = 0; src_i < inst->Instruction.NumSrcRegs; src_i++) {
                  bool signalDummy = false;
                  const struct tgsi_full_src_register *src = &inst->Src[src_i];
                  const char* dst_name = get_register_dst_name(shader_type, dst, dst_i, &signalDummy);
                  if(signalDummy && (ptx_opcodes[opcode].name!="mov")){
                     useDummyDst = true;
                  }
                  const char* src_name = get_register_src_name(shader_type, src, maskBit);
                  if(0==strcmp(dst_name, src_name)){
                     printf("dst=%s, src=%s\n", dst_name, src_name);
                     useDummyDst = true;
                     break;
                  }
               }
               if(useDummyDst) break;
            }
            if(useDummyDst) break;
         }
      }

      writeMask = dst->Register.WriteMask;
      uint dpBit = -1;
      if(dpOp){
        while(writeMask) {
          dpBit++;
          if(1 & writeMask)
            break;
          writeMask >>= 1;
        }
        writeMask = 0xF;
      }

      maskBit = -1;
      while(writeMask) {
        maskBit++;
        bool enabled = (1 & writeMask) != 0;
        writeMask >>= 1;
        if(!enabled) continue;

        for (src_i = 0; src_i < inst->Instruction.NumSrcRegs; src_i++) {
          const struct tgsi_full_src_register *src = &inst->Src[src_i];

          /*if (!first_reg)
            CHR( ',' );
          CHR( ' ' );*/

          char* srcSign = "";
          if (src->Register.Negate)
            srcSign = "-";

          assert(!src->Register.Absolute);
          /*if (src->Register.Absolute)
            CHR( '|' );*/

          //TODO

          //const char * src_swizzle = get_src_swizzle(src, maskBit);
          const char* src_name = get_register_src_name(shader_type, src, maskBit);
          if (strcmp(src_name,"(null)") != 0)
            snprintf((char*) &srcRegNames[src_i], 100, "%s%s", srcSign, src_name);
          free((void*)src_name);

          //first_reg = FALSE;

        }//end for srcs

        //const char* dst_swizzle = tgsi_swizzle_names[maskBit];
        char* dstRegName;
        //char dpReg[100];
        if(dpOp){
          /*strcpy(dpReg, "dpReg");
          strcat(dpReg, tgsi_swizzle_names[maskBit]);
          fprintf(inst_stream, ".reg .f32 %s;\n", dpReg);
          dstRegName = dpReg;*/
          free((void*) ptxInst);
          asprintf(&ptxInst, "%s", "mad.f32");
          dstRegName = (char*)get_register_dst_name(shader_type, dst, dpBit, NULL);
        } else {
          dstRegName = (char*)get_register_dst_name(shader_type, dst, maskBit, NULL);
        }

        if(useDummyDst){
           fprintf(inst_stream, "%s %s%d", ptxInst, "dummy_", maskBit);
        } else {
           fprintf(inst_stream, "%s %s", ptxInst, dstRegName);
        }

        for (src_i = 0; src_i < inst->Instruction.NumSrcRegs; src_i++) {
          fprintf(inst_stream, ", %s", srcRegNames[src_i]);
        }

        if(dpOp){
          //add self
          if(maskBit == 0){
            fprintf(inst_stream, ", 0f00000000");
          } else {
            fprintf(inst_stream, ", %s", dstRegName);
          }
        }
        fprintf(inst_stream, ";\n");

      }//end while

      if(useDummyDst){
         maskBit = -1;
         writeMask = dst->Register.WriteMask;
         while(writeMask) {
            maskBit++;
            bool enabled = (1 & writeMask) != 0;
            writeMask >>= 1;
            if(!enabled) continue;
            char* dstRegName = (char*)get_register_dst_name(shader_type, dst, maskBit, NULL);
            fprintf(inst_stream, "mov.f32 %s, %s%d;\n", dstRegName, "dummy_", maskBit);
         }
      }

      /*maskBit = -1;
      if(dpOp){
        writeMask = dst->Register.WriteMask;
        while(writeMask) {
          maskBit++;
          if((1 & writeMask))
            break;
        }
        for(int i=0; i<4; i++){
          if(i==0){
            fprintf(inst_stream, "mov.f32 %s, %s%s;\n",
                    (char*) get_register_dst_name(dst, maskBit),
                    "dpReg", tgsi_swizzle_names[i]);
          } else {
            fprintf(inst_stream, "add.f32 %s, %s%s;\n",
                    (char*) get_register_dst_name(dst, maskBit),
                    "dpReg", tgsi_swizzle_names[i]);
          }
        }
      }*/

      char* dstRegName = (char*)get_register_dst_name(shader_type, dst, 0, NULL);
      //if writing to color then add stp instructoin afterwards
      if(strcmp(dstRegName, "COLOR0.x") == 0) {
         fprintf(inst_stream, "BLEND_CODE\n");
      }

      //first_reg = FALSE;
   }//end for

   free((void*)ptxInst);


   if (inst->Instruction.Memory) {
     assert(0); //TODO
      uint32_t qualifier = inst->Memory.Qualifier;
      while (qualifier) {
         int bit = ffs(qualifier) - 1;
         qualifier &= ~(1U << bit);
         TXT(", ");
         ENM(bit, tgsi_memory_names);
      }
      if (inst->Memory.Texture) {
         TXT( ", " );
         ENM( inst->Memory.Texture, tgsi_texture_names );
      }
      if (inst->Memory.Format) {
         TXT( ", " );
         TXT( util_format_name(inst->Memory.Format) );
      }
   }

   if (inst->Instruction.Label) { 
      assert(0);; //TODO
      switch (inst->Instruction.Opcode) {
      case TGSI_OPCODE_IF:
      case TGSI_OPCODE_UIF:
      case TGSI_OPCODE_ELSE:
      case TGSI_OPCODE_BGNLOOP:
      case TGSI_OPCODE_ENDLOOP:
      case TGSI_OPCODE_CAL:
      case TGSI_OPCODE_BGNSUB:
         TXT( " :" );
         UID( inst->Label.Label );
         break;
      }
   }

   // update indentation
   /*if (inst->Instruction.Opcode == TGSI_OPCODE_IF ||
       inst->Instruction.Opcode == TGSI_OPCODE_UIF ||
       inst->Instruction.Opcode == TGSI_OPCODE_ELSE ||
       inst->Instruction.Opcode == TGSI_OPCODE_BGNLOOP) {
      //TODO
      //ctx->indentation += indent_spaces;
   }*/

   return TRUE;
}


static boolean
gen_ptx_declaration(
   int shader_type,
   FILE* inst_stream,
   struct tgsi_iterate_context *iter,
   struct tgsi_full_declaration *decl,
   shader_stats_t* shader_stats)
{
   boolean patch = decl->Semantic.Name == TGSI_SEMANTIC_PATCH ||
      decl->Semantic.Name == TGSI_SEMANTIC_TESSINNER ||
      decl->Semantic.Name == TGSI_SEMANTIC_TESSOUTER ||
      decl->Semantic.Name == TGSI_SEMANTIC_PRIMID;

   //TXT( "DCL " );

   const char* file_name = tgsi_file_name(decl->Declaration.File);

   bool genDeclFlag = true;
   genDeclFlag = genDeclFlag && strcmp(file_name, "SVIEW");
   genDeclFlag = genDeclFlag && strcmp(file_name, "SAMP");

   if(!genDeclFlag)
     return TRUE;

   if(!strcmp(file_name, "TEMP")){
     shader_stats->usedRegs++;
   }

   /* all geometry shader inputs and non-patch tessellation shader inputs are
    * two dimensional
    */
   if (decl->Declaration.File == TGSI_FILE_INPUT &&
       (iter->processor.Processor == PIPE_SHADER_GEOMETRY ||
        (!patch &&
         (iter->processor.Processor == PIPE_SHADER_TESS_CTRL ||
          iter->processor.Processor == PIPE_SHADER_TESS_EVAL)))) {
     abort();
      //TXT("[]");
   }

   /* all non-patch tess ctrl shader outputs are two dimensional */
   if (decl->Declaration.File == TGSI_FILE_OUTPUT &&
       !patch &&
       iter->processor.Processor == PIPE_SHADER_TESS_CTRL) {
     abort();
      //TXT("[]");
   }

   bool replace_reg = false;
   char* replace_reg_name = NULL;

   if (decl->Declaration.Semantic) {
      replace_reg = (shader_type == GL_FRAGMENT_SHADER);
      //register_dic_t* sd = (struct register_dic_t*) malloc(sizeof(struct register_dic_t));
      if ( decl->Semantic.Index != 0 ||
          decl->Semantic.Name == TGSI_SEMANTIC_TEXCOORD ||
          decl->Semantic.Name == TGSI_SEMANTIC_GENERIC) {
         //asprintf(&sd->reg_key, "%s%d", tgsi_semantic_names[decl->Semantic.Name], decl->Semantic.Index );
         asprintf(&replace_reg_name, "%s%d", tgsi_semantic_names[decl->Semantic.Name], decl->Semantic.Index );
      } else {
         //asprintf(&sd->reg_key, "%s", tgsi_semantic_names[decl->Semantic.Name]);
         asprintf(&replace_reg_name, "%s", tgsi_semantic_names[decl->Semantic.Name]);
      }

      if (decl->Semantic.StreamX != 0 || decl->Semantic.StreamY != 0 ||
          decl->Semantic.StreamZ != 0 || decl->Semantic.StreamW != 0) {
         abort();
         TXT(", STREAM(");
         UID(decl->Semantic.StreamX);
         TXT(", ");
         UID(decl->Semantic.StreamY);
         TXT(", ");
         UID(decl->Semantic.StreamZ);
         TXT(", ");
         UID(decl->Semantic.StreamW);
         CHR(')');
      }

   }

   if (decl->Declaration.Dimension) {
      /*CHR('[');
      SID(decl->Dim.Index2D);
      CHR(']');*/
   }

   /*CHR('[');
   SID(decl->Range.First);
   if (decl->Range.First != decl->Range.Last) {
     abort();
      TXT("..");
      SID(decl->Range.Last);
   }
   CHR(']');

     _dump_writemask(
      ctx,
      decl->Declaration.UsageMask );*/

   int regsCount = decl->Range.Last - decl->Range.First + 1;
   int regNum;
   uint writemask = decl->Declaration.UsageMask;
   const char* regDef = ".reg .f32";
   if(replace_reg) {
     if(regsCount != 1){
       abort(); //unexpected case
     }
     register_dic_t* sd = (register_dic_t*) malloc(sizeof(register_dic_t));
     asprintf(&sd->reg_key, "%s%d", file_name, decl->Range.First);
     sd->reg_value = replace_reg_name;

     HASH_ADD_KEYPTR( hh, register_dictionary, sd->reg_key, strlen(sd->reg_key), sd );
     //printf("replacing %s with %s\n", sd->reg_key, sd->reg_value);
   } else {
     bool load_vx = (shader_type == GL_VERTEX_SHADER) && (!strcmp(file_name, "IN"));

     //load input register 

     for(regNum = 0; regNum < regsCount; regNum ++){
       int actual_num = regNum + decl->Range.First;
       if (writemask != TGSI_WRITEMASK_XYZW) {
         if (writemask & TGSI_WRITEMASK_X){
           fprintf(inst_stream, "%s %s%d%s;\n", regDef, file_name, actual_num, "x");
           if(load_vx)
              fprintf(inst_stream, "ldv.const.constf32 %s%d%s, VATTRIB%d.%s;\n", file_name, actual_num, "x", actual_num, "x");
         }
         if (writemask & TGSI_WRITEMASK_Y){
           fprintf(inst_stream, "%s %s%d%s;\n", regDef, file_name, actual_num, "y");
           if(load_vx)
              fprintf(inst_stream, "ldv.const.f32 %s%d%s, VATTRIB%d.%s;\n", file_name, actual_num, "y", actual_num, "y");
         }
         if (writemask & TGSI_WRITEMASK_Z){
           fprintf(inst_stream, "%s %s%d%s;\n", regDef, file_name, actual_num, "z");
           if(load_vx)
              fprintf(inst_stream, "ldv.const.f32 %s%d%s, VATTRIB%d.%s;\n", file_name, actual_num, "z", actual_num, "z");
         }
         if (writemask & TGSI_WRITEMASK_W){
           fprintf(inst_stream, "%s %s%d%s;\n", regDef, file_name, actual_num, "w");
           if(load_vx)
              fprintf(inst_stream, "ldv.const.f32 %s%d%s, VATTRIB%d.%s;\n", file_name, actual_num, "w", actual_num, "w");
         }
       } else {
         fprintf(inst_stream, "%s %s%d%s;\n", regDef, file_name, actual_num, "x");
         fprintf(inst_stream, "%s %s%d%s;\n", regDef, file_name, actual_num, "y");
         fprintf(inst_stream, "%s %s%d%s;\n", regDef, file_name, actual_num, "z");
         fprintf(inst_stream, "%s %s%d%s;\n", regDef, file_name, actual_num, "w");
         if(load_vx){
           fprintf(inst_stream, "ldv.const.f32 %s%d%s, VATTRIB%d.%s;\n", file_name, actual_num, "x", actual_num, "x");
           fprintf(inst_stream, "ldv.const.f32 %s%d%s, VATTRIB%d.%s;\n", file_name, actual_num, "y", actual_num, "y");
           fprintf(inst_stream, "ldv.const.f32 %s%d%s, VATTRIB%d.%s;\n", file_name, actual_num, "z", actual_num, "z");
           fprintf(inst_stream, "ldv.const.f32 %s%d%s, VATTRIB%d.%s;\n", file_name, actual_num, "w", actual_num, "w");
         }
       }
     }
   }
   if (strcmp(file_name, "CONST")) {
      for (int i = 0; i < 13; i++)
      {
         fprintf(inst_stream, "%s %s%d%s;\n", regDef, "CONST", i, "x");
         fprintf(inst_stream, "%s %s%d%s;\n", regDef, "CONST", i, "y");
         fprintf(inst_stream, "%s %s%d%s;\n", regDef, "CONST", i, "z");
         fprintf(inst_stream, "%s %s%d%s;\n", regDef, "CONST", i, "w");
      }
   }
   if (decl->Declaration.Array) {
      abort();
      /*TXT( ", ARRAY(" );
      SID(decl->Array.ArrayID);
      CHR(')');*/
   }


   //no need to identify interpolates
   /*if (decl->Declaration.Local){
      abort();
      TXT( ", LOCAL" );
   }*/


   if (decl->Declaration.File == TGSI_FILE_IMAGE) {
     abort();
      /*TXT(", ");
      ENM(decl->Image.Resource, tgsi_texture_names);
      TXT(", ");
      TXT(util_format_name(decl->Image.Format));
      if (decl->Image.Writable)
         TXT(", WR");
      if (decl->Image.Raw)
         TXT(", RAW");*/
   }

   if (decl->Declaration.File == TGSI_FILE_BUFFER) {
     abort();
      /*if (decl->Declaration.Atomic)
         TXT(", ATOMIC");*/
   }

   if (decl->Declaration.File == TGSI_FILE_MEMORY) {
     abort();
      /*switch (decl->Declaration.MemType) {
      //Note: ,GLOBAL is optional / the default
      case TGSI_MEMORY_TYPE_GLOBAL:  TXT(", GLOBAL");  break;
      case TGSI_MEMORY_TYPE_SHARED:  TXT(", SHARED");  break;
      case TGSI_MEMORY_TYPE_PRIVATE: TXT(", PRIVATE"); break;
      case TGSI_MEMORY_TYPE_INPUT:   TXT(", INPUT");   break;
      }
      */
   }

   if (decl->Declaration.File == TGSI_FILE_SAMPLER_VIEW) {
     abort();
      /*TXT(", ");
      ENM(decl->SamplerView.Resource, tgsi_texture_names);
      TXT(", ");
      if ((decl->SamplerView.ReturnTypeX == decl->SamplerView.ReturnTypeY) &&
          (decl->SamplerView.ReturnTypeX == decl->SamplerView.ReturnTypeZ) &&
          (decl->SamplerView.ReturnTypeX == decl->SamplerView.ReturnTypeW)) {
         ENM(decl->SamplerView.ReturnTypeX, tgsi_return_type_names);
      } else {
         ENM(decl->SamplerView.ReturnTypeX, tgsi_return_type_names);
         TXT(", ");
         ENM(decl->SamplerView.ReturnTypeY, tgsi_return_type_names);
         TXT(", ");
         ENM(decl->SamplerView.ReturnTypeZ, tgsi_return_type_names);
         TXT(", ");
         ENM(decl->SamplerView.ReturnTypeW, tgsi_return_type_names);
      }*/
   }


   //no need to identify interpolates
   /*if (decl->Declaration.Interpolate) {
      if (iter->processor.Processor == PIPE_SHADER_FRAGMENT &&
          decl->Declaration.File == TGSI_FILE_INPUT)
      {
         TXT( ", " );
         ENM( decl->Interp.Interpolate, tgsi_interpolate_names );
      }

      if (decl->Interp.Location != TGSI_INTERPOLATE_LOC_CENTER) {
         TXT( ", " );
         ENM( decl->Interp.Location, tgsi_interpolate_locations );
      }

      if (decl->Interp.CylindricalWrap) {
         TXT(", CYLWRAP_");
         if (decl->Interp.CylindricalWrap & TGSI_CYLINDRICAL_WRAP_X) {
            CHR('X');
         }
         if (decl->Interp.CylindricalWrap & TGSI_CYLINDRICAL_WRAP_Y) {
            CHR('Y');
         }
         if (decl->Interp.CylindricalWrap & TGSI_CYLINDRICAL_WRAP_Z) {
            CHR('Z');
         }
         if (decl->Interp.CylindricalWrap & TGSI_CYLINDRICAL_WRAP_W) {
            CHR('W');
         }
      }
      
   }*/

   if (decl->Declaration.Invariant) {
      abort();
      //TXT( ", INVARIANT" );
   }

   //EOL();

   return TRUE;
}



static boolean
gen_ptx_immediate(
   FILE* inst_stream,
   struct tgsi_iterate_context *iter,
   struct tgsi_full_immediate *imm )
{
   struct dump_ctx *ctx = (struct dump_ctx *) iter;
   int im_id = ctx->immno++;

   /*dump_imm_data(iter, imm->u, imm->Immediate.NrTokens - 1,
                 imm->Immediate.DataType);

  dump_imm_data(struct tgsi_iterate_context *iter,
              union tgsi_immediate_data *data,
              unsigned num_tokens,
              unsigned data_type)*/

   unsigned num_tokens = imm->Immediate.NrTokens - 1;
   unsigned i ;

   assert( num_tokens <= 4 );
   for (i = 0; i < num_tokens; i++) {
      char* imm_name;
      asprintf(&imm_name, "%s%d%s", "IMM", im_id, tgsi_swizzle_names[i]);
      //const char* imm_type =  tgsi_immediate_type_names_ptx[imm->Immediate.DataType];
      register_dic_t* sd = (register_dic_t*) malloc(sizeof(register_dic_t));

      sd->reg_key = imm_name;
      //sd->reg_value = replace_reg_name;

      switch (imm->Immediate.DataType) {
      case TGSI_IMM_FLOAT64: {
         union di d;
         d.ui = imm->u[i].Uint | (uint64_t)imm->u[i+1].Uint << 32;
         //DBL( d.d );
         asprintf(&sd->reg_value, "0f%08lx", d.ui);
         //printf("replacing %s with %s\n", sd->reg_key, sd->reg_value);
         i++;
         break;
      }
      case TGSI_IMM_INT64:
      case TGSI_IMM_UINT64: {
         union di d;
         d.ui = imm->u[i].Uint | (uint64_t)imm->u[i+1].Uint << 32;
         //UI64D( d.ui );
         asprintf(&sd->reg_value, "0x%lx", d.ui);
         //printf("replacing %s with %s\n", sd->reg_key, sd->reg_value);
         i++;
         break;
      }
      case TGSI_IMM_FLOAT32:{
         union di d;
         d.ui = imm->u[i].Uint;
         asprintf(&sd->reg_value, "0f%08x", d.ui);
         //printf("replacing %s with %s\n", sd->reg_key, sd->reg_value);
         break;
      }
      case TGSI_IMM_UINT32:{
         union di d;
         d.ui = imm->u[i].Uint;
         asprintf(&sd->reg_value, "0x%x", d.ui);
         //printf("replacing %s with %s\n", sd->reg_key, sd->reg_value);
         break;
      }
      default:
         assert( 0 );
      }

      HASH_ADD_KEYPTR( hh, register_dictionary, sd->reg_key, strlen(sd->reg_key), sd );


      //if (i < num_tokens - 1)
      //   TXT( ", " );
   }
   //TXT( "}" );

   return TRUE;
}


static void add_ptx_head(FILE* inst_stream, int shader_type, int frame_num, int drawcall_num){
    
  fprintf(inst_stream, ".version 2.0\n");
  fprintf(inst_stream, ".target sm_10, map_f64_to_f32\n");
  fprintf(inst_stream, "\n\n");

  int sampler;
  for(sampler = 0; sampler < PIPE_MAX_SHADER_SAMPLER_VIEWS; sampler++){
    fprintf(inst_stream, ".reg .f32 TGSI_SAMP_%d; //dummy sampler def\n", sampler);
  }

  int dummy;
  for(dummy = 0; dummy < 4; dummy++){
    fprintf(inst_stream, ".reg .f32 dummy_%d; //dummy register\n", dummy);
  }

  if(shader_type == GL_FRAGMENT_SHADER) {
    fprintf(inst_stream, ".entry fp%d_%d (.param .u64 __cudaparm_fp%d_%d_outputData){\n",
            frame_num, drawcall_num, frame_num, drawcall_num);
    fprintf(inst_stream, ".reg .pred qflag, fflag;\n");
    fprintf(inst_stream, ".reg .u32 %%color;\n");
    fprintf(inst_stream, "setp.ne.u32 qflag, 0, %%quad_active;\n");
    fprintf(inst_stream, "setp.ne.u32 fflag, 0, %%fragment_active;\n");
    fprintf(inst_stream, "@!qflag exit;\n");
   //TODO check if depth value changed by the shader, if so move depth code to the end
    fprintf(inst_stream, "DEPTH_CODE\n");
  } else if(shader_type == GL_VERTEX_SHADER) {
    fprintf(inst_stream, ".entry vp%d_%d (.param .u64 __cudaparm_vp%d_%d_inputData){\n",
            frame_num, drawcall_num, frame_num, drawcall_num);
    fprintf(inst_stream, "VERTEX_CODE\n");
  } else {
    printf("TGSI to PTX: unsupported shader type %d\n", shader_type);
    abort();
  }
}

static void print_ptx_tail(FILE* inst_stream, int shader_type){
  if(shader_type == GL_FRAGMENT_SHADER) {
    //fprintf(inst_stream, "stp;\n");
    fprintf(inst_stream, "\n}");
  } else if(shader_type == GL_VERTEX_SHADER) {
    fprintf(inst_stream, "\n}");
  } else {
    printf("TGSI to PTX: unsupported shader type %d\n", shader_type);
    abort();
  }
}


char*
generate_tgsi_ptx_code(
   const struct tgsi_token *tokens,
   int shader_type,
   int frame_num,
   int drawcall_num,
   const char* output_dir)
{
   struct dump_ctx ctx;
   memset(&ctx, 0, sizeof(ctx));
   init_dump_ctx(&ctx, 0);


   //char* mid_inst_str;
   //size_t mid_inst_size;
   //FILE* mid_inst_stream = open_memstream(&mid_inst_str, &mid_inst_size);

   char* file_name;
   char* file_name_tgsi;
   FILE* tgsiFile;
   if(shader_type == GL_FRAGMENT_SHADER){
     asprintf(&file_name, "%s/fragment_shader%d_%d.ptx", output_dir, frame_num, drawcall_num);
     asprintf(&file_name_tgsi, "%s/fragment_shader%d_%d.tgsi", output_dir, frame_num, drawcall_num);
     tgsiFile = fopen(file_name_tgsi, "w+");
     tgsi_dump_to_file(tokens, 0, tgsiFile);
   } else if(shader_type == GL_VERTEX_SHADER) {
     asprintf(&file_name, "%s/vertex_shader%d_%d.ptx", output_dir, frame_num, drawcall_num);
     asprintf(&file_name_tgsi, "%s/vertex_shader%d_%d.tgsi", output_dir, frame_num, drawcall_num);
     tgsiFile = fopen(file_name_tgsi, "w+");
     tgsi_dump_to_file(tokens, 0, tgsiFile);
   } else {
     printf("Unsupported shader type %d\n", shader_type);
   }

   fclose(tgsiFile);
   free((void*) file_name_tgsi);

   FILE* mid_inst_stream = fopen(file_name, "w");
   struct tgsi_parse_context parse;
   shader_stats_t shader_stats;
   shader_stats.usedRegs = 0;

   if (tgsi_parse_init( &parse, tokens ) != TGSI_PARSE_OK)
      return FALSE;

   add_ptx_head(mid_inst_stream, shader_type, frame_num, drawcall_num);

   /*ctx->processor = parse.FullHeader.Processor;

   if (ctx->prolog)
      if (!ctx->prolog( ctx ))
         goto fail;*/

   while (!tgsi_parse_end_of_tokens( &parse )) {
      tgsi_parse_token( &parse );

      switch (parse.FullToken.Token.Type) {
      case TGSI_TOKEN_TYPE_INSTRUCTION:
        gen_ptx_instruction(shader_type, mid_inst_stream, &parse.FullToken.FullInstruction );
        break;

      case TGSI_TOKEN_TYPE_DECLARATION:
         gen_ptx_declaration(shader_type, mid_inst_stream, &ctx.iter, &parse.FullToken.FullDeclaration, &shader_stats);
         break;

      case TGSI_TOKEN_TYPE_IMMEDIATE:
         gen_ptx_immediate(mid_inst_stream, &ctx.iter, &parse.FullToken.FullImmediate );
         break;

      case TGSI_TOKEN_TYPE_PROPERTY:
         printf("TGSI to PTX: ignoring property %s\n",
                tgsi_property_names[parse.FullToken.FullProperty.Property.PropertyName]);
         /*if (ctx->iterate_property)
            if (!ctx->iterate_property( ctx,  &parse.FullToken.FullProperty ))
               goto fail;*/
         break;

      default:
         assert( 0 );
      }
   }

   /*if (ctx->epilog)
      if (!ctx->epilog( ctx ))
         goto fail;*/

   /*tgsi_parse_free( &parse );
   return TRUE;*/

   print_ptx_tail(mid_inst_stream, shader_type);

   fclose(mid_inst_stream);

   free((void*) file_name);

   //printf("%s", mid_inst_str);

   printf("%d used regs %d\n", shader_type, shader_stats.usedRegs);
   //each vector register is 4 scalar regs
   gpgpusimSetShaderRegs(shader_type, shader_stats.usedRegs*4);

   return NULL;
}
