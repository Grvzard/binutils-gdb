/* tc-mips.c -- assemble code for a MIPS chip.
   Copyright 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002,
   2003, 2004 Free Software Foundation, Inc.
   Contributed by the OSF and Ralph Campbell.
   Written by Keith Knowles and Ralph Campbell, working independently.
   Modified for ECOFF and R4000 support by Ian Lance Taylor of Cygnus
   Support.

   This file is part of GAS.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.  */

#include "as.h"
#include "config.h"
#include "subsegs.h"
#include "safe-ctype.h"

#include <stdarg.h>

#include "opcode/mips.h"
#include "itbl-ops.h"
#include "dwarf2dbg.h"

#ifdef DEBUG
#define DBG(x) printf x
#else
#define DBG(x)
#endif

#ifdef OBJ_MAYBE_ELF
/* Clean up namespace so we can include obj-elf.h too.  */
static int mips_output_flavor (void);
static int mips_output_flavor (void) { return OUTPUT_FLAVOR; }
#undef OBJ_PROCESS_STAB
#undef OUTPUT_FLAVOR
#undef S_GET_ALIGN
#undef S_GET_SIZE
#undef S_SET_ALIGN
#undef S_SET_SIZE
#undef obj_frob_file
#undef obj_frob_file_after_relocs
#undef obj_frob_symbol
#undef obj_pop_insert
#undef obj_sec_sym_ok_for_reloc
#undef OBJ_COPY_SYMBOL_ATTRIBUTES

#include "obj-elf.h"
/* Fix any of them that we actually care about.  */
#undef OUTPUT_FLAVOR
#define OUTPUT_FLAVOR mips_output_flavor()
#endif

#if defined (OBJ_ELF)
#include "elf/mips.h"
#endif

#ifndef ECOFF_DEBUGGING
#define NO_ECOFF_DEBUGGING
#define ECOFF_DEBUGGING 0
#endif

int mips_flag_mdebug = -1;

/* Control generation of .pdr sections.  Off by default on IRIX: the native
   linker doesn't know about and discards them, but relocations against them
   remain, leading to rld crashes.  */
#ifdef TE_IRIX
int mips_flag_pdr = FALSE;
#else
int mips_flag_pdr = TRUE;
#endif

#include "ecoff.h"

#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)
static char *mips_regmask_frag;
#endif

#define ZERO 0
#define AT  1
#define TREG 24
#define PIC_CALL_REG 25
#define KT0 26
#define KT1 27
#define GP  28
#define SP  29
#define FP  30
#define RA  31

#define ILLEGAL_REG (32)

/* Allow override of standard little-endian ECOFF format.  */

#ifndef ECOFF_LITTLE_FORMAT
#define ECOFF_LITTLE_FORMAT "ecoff-littlemips"
#endif

extern int target_big_endian;

/* The name of the readonly data section.  */
#define RDATA_SECTION_NAME (OUTPUT_FLAVOR == bfd_target_aout_flavour \
			    ? ".data" \
			    : OUTPUT_FLAVOR == bfd_target_ecoff_flavour \
			    ? ".rdata" \
			    : OUTPUT_FLAVOR == bfd_target_coff_flavour \
			    ? ".rdata" \
			    : OUTPUT_FLAVOR == bfd_target_elf_flavour \
			    ? ".rodata" \
			    : (abort (), ""))

/* The ABI to use.  */
enum mips_abi_level
{
  NO_ABI = 0,
  O32_ABI,
  O64_ABI,
  N32_ABI,
  N64_ABI,
  EABI_ABI
};

/* MIPS ABI we are using for this output file.  */
static enum mips_abi_level mips_abi = NO_ABI;

/* Whether or not we have code that can call pic code.  */
int mips_abicalls = FALSE;

/* This is the set of options which may be modified by the .set
   pseudo-op.  We use a struct so that .set push and .set pop are more
   reliable.  */

struct mips_set_options
{
  /* MIPS ISA (Instruction Set Architecture) level.  This is set to -1
     if it has not been initialized.  Changed by `.set mipsN', and the
     -mipsN command line option, and the default CPU.  */
  int isa;
  /* Enabled Application Specific Extensions (ASEs).  These are set to -1
     if they have not been initialized.  Changed by `.set <asename>', by
     command line options, and based on the default architecture.  */
  int ase_mips3d;
  int ase_mdmx;
  /* Whether we are assembling for the mips16 processor.  0 if we are
     not, 1 if we are, and -1 if the value has not been initialized.
     Changed by `.set mips16' and `.set nomips16', and the -mips16 and
     -nomips16 command line options, and the default CPU.  */
  int mips16;
  /* Non-zero if we should not reorder instructions.  Changed by `.set
     reorder' and `.set noreorder'.  */
  int noreorder;
  /* Non-zero if we should not permit the $at ($1) register to be used
     in instructions.  Changed by `.set at' and `.set noat'.  */
  int noat;
  /* Non-zero if we should warn when a macro instruction expands into
     more than one machine instruction.  Changed by `.set nomacro' and
     `.set macro'.  */
  int warn_about_macros;
  /* Non-zero if we should not move instructions.  Changed by `.set
     move', `.set volatile', `.set nomove', and `.set novolatile'.  */
  int nomove;
  /* Non-zero if we should not optimize branches by moving the target
     of the branch into the delay slot.  Actually, we don't perform
     this optimization anyhow.  Changed by `.set bopt' and `.set
     nobopt'.  */
  int nobopt;
  /* Non-zero if we should not autoextend mips16 instructions.
     Changed by `.set autoextend' and `.set noautoextend'.  */
  int noautoextend;
  /* Restrict general purpose registers and floating point registers
     to 32 bit.  This is initially determined when -mgp32 or -mfp32
     is passed but can changed if the assembler code uses .set mipsN.  */
  int gp32;
  int fp32;
  /* MIPS architecture (CPU) type.  Changed by .set arch=FOO, the -march
     command line option, and the default CPU.  */
  int arch;
};

/* True if -mgp32 was passed.  */
static int file_mips_gp32 = -1;

/* True if -mfp32 was passed.  */
static int file_mips_fp32 = -1;

/* This is the struct we use to hold the current set of options.  Note
   that we must set the isa field to ISA_UNKNOWN and the ASE fields to
   -1 to indicate that they have not been initialized.  */

static struct mips_set_options mips_opts =
{
  ISA_UNKNOWN, -1, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, CPU_UNKNOWN
};

/* These variables are filled in with the masks of registers used.
   The object format code reads them and puts them in the appropriate
   place.  */
unsigned long mips_gprmask;
unsigned long mips_cprmask[4];

/* MIPS ISA we are using for this output file.  */
static int file_mips_isa = ISA_UNKNOWN;

/* True if -mips16 was passed or implied by arguments passed on the
   command line (e.g., by -march).  */
static int file_ase_mips16;

/* True if -mips3d was passed or implied by arguments passed on the
   command line (e.g., by -march).  */
static int file_ase_mips3d;

/* True if -mdmx was passed or implied by arguments passed on the
   command line (e.g., by -march).  */
static int file_ase_mdmx;

/* The argument of the -march= flag.  The architecture we are assembling.  */
static int file_mips_arch = CPU_UNKNOWN;
static const char *mips_arch_string;

/* The argument of the -mtune= flag.  The architecture for which we
   are optimizing.  */
static int mips_tune = CPU_UNKNOWN;
static const char *mips_tune_string;

/* True when generating 32-bit code for a 64-bit processor.  */
static int mips_32bitmode = 0;

/* True if the given ABI requires 32-bit registers.  */
#define ABI_NEEDS_32BIT_REGS(ABI) ((ABI) == O32_ABI)

/* Likewise 64-bit registers.  */
#define ABI_NEEDS_64BIT_REGS(ABI) \
  ((ABI) == N32_ABI 		  \
   || (ABI) == N64_ABI		  \
   || (ABI) == O64_ABI)

/*  Return true if ISA supports 64 bit gp register instructions.  */
#define ISA_HAS_64BIT_REGS(ISA) (    \
   (ISA) == ISA_MIPS3                \
   || (ISA) == ISA_MIPS4             \
   || (ISA) == ISA_MIPS5             \
   || (ISA) == ISA_MIPS64            \
   || (ISA) == ISA_MIPS64R2          \
   )

/* Return true if ISA supports 64-bit right rotate (dror et al.)
   instructions.  */
#define ISA_HAS_DROR(ISA) (	\
   (ISA) == ISA_MIPS64R2	\
   )

/* Return true if ISA supports 32-bit right rotate (ror et al.)
   instructions.  */
#define ISA_HAS_ROR(ISA) (	\
   (ISA) == ISA_MIPS32R2	\
   || (ISA) == ISA_MIPS64R2	\
   )

#define HAVE_32BIT_GPRS		                   \
    (mips_opts.gp32 || ! ISA_HAS_64BIT_REGS (mips_opts.isa))

#define HAVE_32BIT_FPRS                            \
    (mips_opts.fp32 || ! ISA_HAS_64BIT_REGS (mips_opts.isa))

#define HAVE_64BIT_GPRS (! HAVE_32BIT_GPRS)
#define HAVE_64BIT_FPRS (! HAVE_32BIT_FPRS)

#define HAVE_NEWABI (mips_abi == N32_ABI || mips_abi == N64_ABI)

#define HAVE_64BIT_OBJECTS (mips_abi == N64_ABI)

/* We can only have 64bit addresses if the object file format
   supports it.  */
#define HAVE_32BIT_ADDRESSES                           \
   (HAVE_32BIT_GPRS                                    \
    || ((bfd_arch_bits_per_address (stdoutput) == 32   \
         || ! HAVE_64BIT_OBJECTS)                      \
        && mips_pic != EMBEDDED_PIC))

#define HAVE_64BIT_ADDRESSES (! HAVE_32BIT_ADDRESSES)

/* Addresses are loaded in different ways, depending on the address size
   in use.  The n32 ABI Documentation also mandates the use of additions
   with overflow checking, but existing implementations don't follow it.  */
#define ADDRESS_ADD_INSN						\
   (HAVE_32BIT_ADDRESSES ? "addu" : "daddu")

#define ADDRESS_ADDI_INSN						\
   (HAVE_32BIT_ADDRESSES ? "addiu" : "daddiu")

#define ADDRESS_LOAD_INSN						\
   (HAVE_32BIT_ADDRESSES ? "lw" : "ld")

#define ADDRESS_STORE_INSN						\
   (HAVE_32BIT_ADDRESSES ? "sw" : "sd")

/* Return true if the given CPU supports the MIPS16 ASE.  */
#define CPU_HAS_MIPS16(cpu)						\
   (strncmp (TARGET_CPU, "mips16", sizeof ("mips16") - 1) == 0		\
    || strncmp (TARGET_CANONICAL, "mips-lsi-elf", sizeof ("mips-lsi-elf") - 1) == 0)

/* Return true if the given CPU supports the MIPS3D ASE.  */
#define CPU_HAS_MIPS3D(cpu)	((cpu) == CPU_SB1      \
				 )

/* Return true if the given CPU supports the MDMX ASE.  */
#define CPU_HAS_MDMX(cpu)	(FALSE                 \
				 )

/* True if CPU has a dror instruction.  */
#define CPU_HAS_DROR(CPU)	((CPU) == CPU_VR5400 || (CPU) == CPU_VR5500)

/* True if CPU has a ror instruction.  */
#define CPU_HAS_ROR(CPU)	CPU_HAS_DROR (CPU)

/* True if mflo and mfhi can be immediately followed by instructions
   which write to the HI and LO registers.

   According to MIPS specifications, MIPS ISAs I, II, and III need
   (at least) two instructions between the reads of HI/LO and
   instructions which write them, and later ISAs do not.  Contradicting
   the MIPS specifications, some MIPS IV processor user manuals (e.g.
   the UM for the NEC Vr5000) document needing the instructions between
   HI/LO reads and writes, as well.  Therefore, we declare only MIPS32,
   MIPS64 and later ISAs to have the interlocks, plus any specific
   earlier-ISA CPUs for which CPU documentation declares that the
   instructions are really interlocked.  */
#define hilo_interlocks \
  (mips_opts.isa == ISA_MIPS32                        \
   || mips_opts.isa == ISA_MIPS32R2                   \
   || mips_opts.isa == ISA_MIPS64                     \
   || mips_opts.isa == ISA_MIPS64R2                   \
   || mips_opts.arch == CPU_R4010                     \
   || mips_opts.arch == CPU_R10000                    \
   || mips_opts.arch == CPU_R12000                    \
   || mips_opts.arch == CPU_RM7000                    \
   || mips_opts.arch == CPU_SB1                       \
   || mips_opts.arch == CPU_VR5500                    \
   )

/* Whether the processor uses hardware interlocks to protect reads
   from the GPRs after they are loaded from memory, and thus does not
   require nops to be inserted.  This applies to instructions marked
   INSN_LOAD_MEMORY_DELAY.  These nops are only required at MIPS ISA
   level I.  */
#define gpr_interlocks \
  (mips_opts.isa != ISA_MIPS1  \
   || mips_opts.arch == CPU_VR5400  \
   || mips_opts.arch == CPU_VR5500  \
   || mips_opts.arch == CPU_R3900)

/* Whether the processor uses hardware interlocks to avoid delays
   required by coprocessor instructions, and thus does not require
   nops to be inserted.  This applies to instructions marked
   INSN_LOAD_COPROC_DELAY, INSN_COPROC_MOVE_DELAY, and to delays
   between instructions marked INSN_WRITE_COND_CODE and ones marked
   INSN_READ_COND_CODE.  These nops are only required at MIPS ISA
   levels I, II, and III.  */
/* Itbl support may require additional care here.  */
#define cop_interlocks                                \
  ((mips_opts.isa != ISA_MIPS1                        \
    && mips_opts.isa != ISA_MIPS2                     \
    && mips_opts.isa != ISA_MIPS3)                    \
   || mips_opts.arch == CPU_R4300                     \
   || mips_opts.arch == CPU_VR5400                    \
   || mips_opts.arch == CPU_VR5500                    \
   || mips_opts.arch == CPU_SB1                       \
   )

/* Whether the processor uses hardware interlocks to protect reads
   from coprocessor registers after they are loaded from memory, and
   thus does not require nops to be inserted.  This applies to
   instructions marked INSN_COPROC_MEMORY_DELAY.  These nops are only
   requires at MIPS ISA level I.  */
#define cop_mem_interlocks (mips_opts.isa != ISA_MIPS1)

/* Is this a mfhi or mflo instruction?  */
#define MF_HILO_INSN(PINFO) \
          ((PINFO & INSN_READ_HI) || (PINFO & INSN_READ_LO))

/* MIPS PIC level.  */

enum mips_pic_level mips_pic;

/* 1 if we should generate 32 bit offsets from the $gp register in
   SVR4_PIC mode.  Currently has no meaning in other modes.  */
static int mips_big_got = 0;

/* 1 if trap instructions should used for overflow rather than break
   instructions.  */
static int mips_trap = 0;

/* 1 if double width floating point constants should not be constructed
   by assembling two single width halves into two single width floating
   point registers which just happen to alias the double width destination
   register.  On some architectures this aliasing can be disabled by a bit
   in the status register, and the setting of this bit cannot be determined
   automatically at assemble time.  */
static int mips_disable_float_construction;

/* Non-zero if any .set noreorder directives were used.  */

static int mips_any_noreorder;

/* Non-zero if nops should be inserted when the register referenced in
   an mfhi/mflo instruction is read in the next two instructions.  */
static int mips_7000_hilo_fix;

/* The size of the small data section.  */
static unsigned int g_switch_value = 8;
/* Whether the -G option was used.  */
static int g_switch_seen = 0;

#define N_RMASK 0xc4
#define N_VFP   0xd4

/* If we can determine in advance that GP optimization won't be
   possible, we can skip the relaxation stuff that tries to produce
   GP-relative references.  This makes delay slot optimization work
   better.

   This function can only provide a guess, but it seems to work for
   gcc output.  It needs to guess right for gcc, otherwise gcc
   will put what it thinks is a GP-relative instruction in a branch
   delay slot.

   I don't know if a fix is needed for the SVR4_PIC mode.  I've only
   fixed it for the non-PIC mode.  KR 95/04/07  */
static int nopic_need_relax (symbolS *, int);

/* handle of the OPCODE hash table */
static struct hash_control *op_hash = NULL;

/* The opcode hash table we use for the mips16.  */
static struct hash_control *mips16_op_hash = NULL;

/* This array holds the chars that always start a comment.  If the
    pre-processor is disabled, these aren't very useful */
const char comment_chars[] = "#";

/* This array holds the chars that only start a comment at the beginning of
   a line.  If the line seems to have the form '# 123 filename'
   .line and .file directives will appear in the pre-processed output */
/* Note that input_file.c hand checks for '#' at the beginning of the
   first line of the input file.  This is because the compiler outputs
   #NO_APP at the beginning of its output.  */
/* Also note that C style comments are always supported.  */
const char line_comment_chars[] = "#";

/* This array holds machine specific line separator characters.  */
const char line_separator_chars[] = ";";

/* Chars that can be used to separate mant from exp in floating point nums */
const char EXP_CHARS[] = "eE";

/* Chars that mean this number is a floating point constant */
/* As in 0f12.456 */
/* or    0d1.2345e12 */
const char FLT_CHARS[] = "rRsSfFdDxXpP";

/* Also be aware that MAXIMUM_NUMBER_OF_CHARS_FOR_FLOAT may have to be
   changed in read.c .  Ideally it shouldn't have to know about it at all,
   but nothing is ideal around here.
 */

static char *insn_error;

static int auto_align = 1;

/* When outputting SVR4 PIC code, the assembler needs to know the
   offset in the stack frame from which to restore the $gp register.
   This is set by the .cprestore pseudo-op, and saved in this
   variable.  */
static offsetT mips_cprestore_offset = -1;

/* Similar for NewABI PIC code, where $gp is callee-saved.  NewABI has some
   more optimizations, it can use a register value instead of a memory-saved
   offset and even an other register than $gp as global pointer.  */
static offsetT mips_cpreturn_offset = -1;
static int mips_cpreturn_register = -1;
static int mips_gp_register = GP;
static int mips_gprel_offset = 0;

/* Whether mips_cprestore_offset has been set in the current function
   (or whether it has already been warned about, if not).  */
static int mips_cprestore_valid = 0;

/* This is the register which holds the stack frame, as set by the
   .frame pseudo-op.  This is needed to implement .cprestore.  */
static int mips_frame_reg = SP;

/* Whether mips_frame_reg has been set in the current function
   (or whether it has already been warned about, if not).  */
static int mips_frame_reg_valid = 0;

/* To output NOP instructions correctly, we need to keep information
   about the previous two instructions.  */

/* Whether we are optimizing.  The default value of 2 means to remove
   unneeded NOPs and swap branch instructions when possible.  A value
   of 1 means to not swap branches.  A value of 0 means to always
   insert NOPs.  */
static int mips_optimize = 2;

/* Debugging level.  -g sets this to 2.  -gN sets this to N.  -g0 is
   equivalent to seeing no -g option at all.  */
static int mips_debug = 0;

/* The previous instruction.  */
static struct mips_cl_insn prev_insn;

/* The instruction before prev_insn.  */
static struct mips_cl_insn prev_prev_insn;

/* If we don't want information for prev_insn or prev_prev_insn, we
   point the insn_mo field at this dummy integer.  */
static const struct mips_opcode dummy_opcode = { NULL, NULL, 0, 0, 0, 0 };

/* Non-zero if prev_insn is valid.  */
static int prev_insn_valid;

/* The frag for the previous instruction.  */
static struct frag *prev_insn_frag;

/* The offset into prev_insn_frag for the previous instruction.  */
static long prev_insn_where;

/* The reloc type for the previous instruction, if any.  */
static bfd_reloc_code_real_type prev_insn_reloc_type[3];

/* The reloc for the previous instruction, if any.  */
static fixS *prev_insn_fixp[3];

/* Non-zero if the previous instruction was in a delay slot.  */
static int prev_insn_is_delay_slot;

/* Non-zero if the previous instruction was in a .set noreorder.  */
static int prev_insn_unreordered;

/* Non-zero if the previous instruction uses an extend opcode (if
   mips16).  */
static int prev_insn_extended;

/* Non-zero if the previous previous instruction was in a .set
   noreorder.  */
static int prev_prev_insn_unreordered;

/* If this is set, it points to a frag holding nop instructions which
   were inserted before the start of a noreorder section.  If those
   nops turn out to be unnecessary, the size of the frag can be
   decreased.  */
static fragS *prev_nop_frag;

/* The number of nop instructions we created in prev_nop_frag.  */
static int prev_nop_frag_holds;

/* The number of nop instructions that we know we need in
   prev_nop_frag.  */
static int prev_nop_frag_required;

/* The number of instructions we've seen since prev_nop_frag.  */
static int prev_nop_frag_since;

/* For ECOFF and ELF, relocations against symbols are done in two
   parts, with a HI relocation and a LO relocation.  Each relocation
   has only 16 bits of space to store an addend.  This means that in
   order for the linker to handle carries correctly, it must be able
   to locate both the HI and the LO relocation.  This means that the
   relocations must appear in order in the relocation table.

   In order to implement this, we keep track of each unmatched HI
   relocation.  We then sort them so that they immediately precede the
   corresponding LO relocation.  */

struct mips_hi_fixup
{
  /* Next HI fixup.  */
  struct mips_hi_fixup *next;
  /* This fixup.  */
  fixS *fixp;
  /* The section this fixup is in.  */
  segT seg;
};

/* The list of unmatched HI relocs.  */

static struct mips_hi_fixup *mips_hi_fixup_list;

/* The frag containing the last explicit relocation operator.
   Null if explicit relocations have not been used.  */

static fragS *prev_reloc_op_frag;

/* Map normal MIPS register numbers to mips16 register numbers.  */

#define X ILLEGAL_REG
static const int mips32_to_16_reg_map[] =
{
  X, X, 2, 3, 4, 5, 6, 7,
  X, X, X, X, X, X, X, X,
  0, 1, X, X, X, X, X, X,
  X, X, X, X, X, X, X, X
};
#undef X

/* Map mips16 register numbers to normal MIPS register numbers.  */

static const unsigned int mips16_to_32_reg_map[] =
{
  16, 17, 2, 3, 4, 5, 6, 7
};

static int mips_fix_4122_bugs;

/* We don't relax branches by default, since this causes us to expand
   `la .l2 - .l1' if there's a branch between .l1 and .l2, because we
   fail to compute the offset before expanding the macro to the most
   efficient expansion.  */

static int mips_relax_branch;

/* The expansion of many macros depends on the type of symbol that
   they refer to.  For example, when generating position-dependent code,
   a macro that refers to a symbol may have two different expansions,
   one which uses GP-relative addresses and one which uses absolute
   addresses.  When generating SVR4-style PIC, a macro may have
   different expansions for local and global symbols.

   We handle these situations by generating both sequences and putting
   them in variant frags.  In position-dependent code, the first sequence
   will be the GP-relative one and the second sequence will be the
   absolute one.  In SVR4 PIC, the first sequence will be for global
   symbols and the second will be for local symbols.

   The frag's "subtype" is RELAX_ENCODE (FIRST, SECOND), where FIRST and
   SECOND are the lengths of the two sequences in bytes.  These fields
   can be extracted using RELAX_FIRST() and RELAX_SECOND().  In addition,
   the subtype has the following flags:

   RELAX_USE_SECOND
	Set if it has been decided that we should use the second
	sequence instead of the first.

   RELAX_SECOND_LONGER
	Set in the first variant frag if the macro's second implementation
	is longer than its first.  This refers to the macro as a whole,
	not an individual relaxation.

   RELAX_NOMACRO
	Set in the first variant frag if the macro appeared in a .set nomacro
	block and if one alternative requires a warning but the other does not.

   RELAX_DELAY_SLOT
	Like RELAX_NOMACRO, but indicates that the macro appears in a branch
	delay slot.

   The frag's "opcode" points to the first fixup for relaxable code.

   Relaxable macros are generated using a sequence such as:

      relax_start (SYMBOL);
      ... generate first expansion ...
      relax_switch ();
      ... generate second expansion ...
      relax_end ();

   The code and fixups for the unwanted alternative are discarded
   by md_convert_frag.  */
#define RELAX_ENCODE(FIRST, SECOND) (((FIRST) << 8) | (SECOND))

#define RELAX_FIRST(X) (((X) >> 8) & 0xff)
#define RELAX_SECOND(X) ((X) & 0xff)
#define RELAX_USE_SECOND 0x10000
#define RELAX_SECOND_LONGER 0x20000
#define RELAX_NOMACRO 0x40000
#define RELAX_DELAY_SLOT 0x80000

/* Branch without likely bit.  If label is out of range, we turn:

 	beq reg1, reg2, label
	delay slot

   into

        bne reg1, reg2, 0f
        nop
        j label
     0: delay slot

   with the following opcode replacements:

	beq <-> bne
	blez <-> bgtz
	bltz <-> bgez
	bc1f <-> bc1t

	bltzal <-> bgezal  (with jal label instead of j label)

   Even though keeping the delay slot instruction in the delay slot of
   the branch would be more efficient, it would be very tricky to do
   correctly, because we'd have to introduce a variable frag *after*
   the delay slot instruction, and expand that instead.  Let's do it
   the easy way for now, even if the branch-not-taken case now costs
   one additional instruction.  Out-of-range branches are not supposed
   to be common, anyway.

   Branch likely.  If label is out of range, we turn:

	beql reg1, reg2, label
	delay slot (annulled if branch not taken)

   into

        beql reg1, reg2, 1f
        nop
        beql $0, $0, 2f
        nop
     1: j[al] label
        delay slot (executed only if branch taken)
     2:

   It would be possible to generate a shorter sequence by losing the
   likely bit, generating something like:

	bne reg1, reg2, 0f
	nop
	j[al] label
	delay slot (executed only if branch taken)
     0:

	beql -> bne
	bnel -> beq
	blezl -> bgtz
	bgtzl -> blez
	bltzl -> bgez
	bgezl -> bltz
	bc1fl -> bc1t
	bc1tl -> bc1f

	bltzall -> bgezal  (with jal label instead of j label)
	bgezall -> bltzal  (ditto)


   but it's not clear that it would actually improve performance.  */
#define RELAX_BRANCH_ENCODE(uncond, likely, link, toofar) \
  ((relax_substateT) \
   (0xc0000000 \
    | ((toofar) ? 1 : 0) \
    | ((link) ? 2 : 0) \
    | ((likely) ? 4 : 0) \
    | ((uncond) ? 8 : 0)))
#define RELAX_BRANCH_P(i) (((i) & 0xf0000000) == 0xc0000000)
#define RELAX_BRANCH_UNCOND(i) (((i) & 8) != 0)
#define RELAX_BRANCH_LIKELY(i) (((i) & 4) != 0)
#define RELAX_BRANCH_LINK(i) (((i) & 2) != 0)
#define RELAX_BRANCH_TOOFAR(i) (((i) & 1) != 0)

/* For mips16 code, we use an entirely different form of relaxation.
   mips16 supports two versions of most instructions which take
   immediate values: a small one which takes some small value, and a
   larger one which takes a 16 bit value.  Since branches also follow
   this pattern, relaxing these values is required.

   We can assemble both mips16 and normal MIPS code in a single
   object.  Therefore, we need to support this type of relaxation at
   the same time that we support the relaxation described above.  We
   use the high bit of the subtype field to distinguish these cases.

   The information we store for this type of relaxation is the
   argument code found in the opcode file for this relocation, whether
   the user explicitly requested a small or extended form, and whether
   the relocation is in a jump or jal delay slot.  That tells us the
   size of the value, and how it should be stored.  We also store
   whether the fragment is considered to be extended or not.  We also
   store whether this is known to be a branch to a different section,
   whether we have tried to relax this frag yet, and whether we have
   ever extended a PC relative fragment because of a shift count.  */
#define RELAX_MIPS16_ENCODE(type, small, ext, dslot, jal_dslot)	\
  (0x80000000							\
   | ((type) & 0xff)						\
   | ((small) ? 0x100 : 0)					\
   | ((ext) ? 0x200 : 0)					\
   | ((dslot) ? 0x400 : 0)					\
   | ((jal_dslot) ? 0x800 : 0))
#define RELAX_MIPS16_P(i) (((i) & 0xc0000000) == 0x80000000)
#define RELAX_MIPS16_TYPE(i) ((i) & 0xff)
#define RELAX_MIPS16_USER_SMALL(i) (((i) & 0x100) != 0)
#define RELAX_MIPS16_USER_EXT(i) (((i) & 0x200) != 0)
#define RELAX_MIPS16_DSLOT(i) (((i) & 0x400) != 0)
#define RELAX_MIPS16_JAL_DSLOT(i) (((i) & 0x800) != 0)
#define RELAX_MIPS16_EXTENDED(i) (((i) & 0x1000) != 0)
#define RELAX_MIPS16_MARK_EXTENDED(i) ((i) | 0x1000)
#define RELAX_MIPS16_CLEAR_EXTENDED(i) ((i) &~ 0x1000)
#define RELAX_MIPS16_LONG_BRANCH(i) (((i) & 0x2000) != 0)
#define RELAX_MIPS16_MARK_LONG_BRANCH(i) ((i) | 0x2000)
#define RELAX_MIPS16_CLEAR_LONG_BRANCH(i) ((i) &~ 0x2000)

/* Is the given value a sign-extended 32-bit value?  */
#define IS_SEXT_32BIT_NUM(x)						\
  (((x) &~ (offsetT) 0x7fffffff) == 0					\
   || (((x) &~ (offsetT) 0x7fffffff) == ~ (offsetT) 0x7fffffff))

/* Is the given value a sign-extended 16-bit value?  */
#define IS_SEXT_16BIT_NUM(x)						\
  (((x) &~ (offsetT) 0x7fff) == 0					\
   || (((x) &~ (offsetT) 0x7fff) == ~ (offsetT) 0x7fff))


/* Global variables used when generating relaxable macros.  See the
   comment above RELAX_ENCODE for more details about how relaxation
   is used.  */
static struct {
  /* 0 if we're not emitting a relaxable macro.
     1 if we're emitting the first of the two relaxation alternatives.
     2 if we're emitting the second alternative.  */
  int sequence;

  /* The first relaxable fixup in the current frag.  (In other words,
     the first fixup that refers to relaxable code.)  */
  fixS *first_fixup;

  /* sizes[0] says how many bytes of the first alternative are stored in
     the current frag.  Likewise sizes[1] for the second alternative.  */
  unsigned int sizes[2];

  /* The symbol on which the choice of sequence depends.  */
  symbolS *symbol;
} mips_relax;

/* Global variables used to decide whether a macro needs a warning.  */
static struct {
  /* True if the macro is in a branch delay slot.  */
  bfd_boolean delay_slot_p;

  /* For relaxable macros, sizes[0] is the length of the first alternative
     in bytes and sizes[1] is the length of the second alternative.
     For non-relaxable macros, both elements give the length of the
     macro in bytes.  */
  unsigned int sizes[2];

  /* The first variant frag for this macro.  */
  fragS *first_frag;
} mips_macro_warning;

/* Prototypes for static functions.  */

#define internalError()							\
    as_fatal (_("internal Error, line %d, %s"), __LINE__, __FILE__)

enum mips_regclass { MIPS_GR_REG, MIPS_FP_REG, MIPS16_REG };

static void append_insn
  (struct mips_cl_insn *ip, expressionS *p, bfd_reloc_code_real_type *r);
static void mips_no_prev_insn (int);
static void mips16_macro_build
  (expressionS *, const char *, const char *, va_list);
static void load_register (int, expressionS *, int);
static void macro_start (void);
static void macro_end (void);
static void macro (struct mips_cl_insn * ip);
static void mips16_macro (struct mips_cl_insn * ip);
#ifdef LOSING_COMPILER
static void macro2 (struct mips_cl_insn * ip);
#endif
static void mips_ip (char *str, struct mips_cl_insn * ip);
static void mips16_ip (char *str, struct mips_cl_insn * ip);
static void mips16_immed
  (char *, unsigned int, int, offsetT, bfd_boolean, bfd_boolean, bfd_boolean,
   unsigned long *, bfd_boolean *, unsigned short *);
static size_t my_getSmallExpression
  (expressionS *, bfd_reloc_code_real_type *, char *);
static void my_getExpression (expressionS *, char *);
static void s_align (int);
static void s_change_sec (int);
static void s_change_section (int);
static void s_cons (int);
static void s_float_cons (int);
static void s_mips_globl (int);
static void s_option (int);
static void s_mipsset (int);
static void s_abicalls (int);
static void s_cpload (int);
static void s_cpsetup (int);
static void s_cplocal (int);
static void s_cprestore (int);
static void s_cpreturn (int);
static void s_gpvalue (int);
static void s_gpword (int);
static void s_gpdword (int);
static void s_cpadd (int);
static void s_insn (int);
static void md_obj_begin (void);
static void md_obj_end (void);
static void s_mips_ent (int);
static void s_mips_end (int);
static void s_mips_frame (int);
static void s_mips_mask (int reg_type);
static void s_mips_stab (int);
static void s_mips_weakext (int);
static void s_mips_file (int);
static void s_mips_loc (int);
static bfd_boolean pic_need_relax (symbolS *, asection *);
static int relaxed_branch_length (fragS *, asection *, int);
static int validate_mips_insn (const struct mips_opcode *);

/* Table and functions used to map between CPU/ISA names, and
   ISA levels, and CPU numbers.  */

struct mips_cpu_info
{
  const char *name;           /* CPU or ISA name.  */
  int is_isa;                 /* Is this an ISA?  (If 0, a CPU.) */
  int isa;                    /* ISA level.  */
  int cpu;                    /* CPU number (default CPU if ISA).  */
};

static const struct mips_cpu_info *mips_parse_cpu (const char *, const char *);
static const struct mips_cpu_info *mips_cpu_info_from_isa (int);
static const struct mips_cpu_info *mips_cpu_info_from_arch (int);

/* Pseudo-op table.

   The following pseudo-ops from the Kane and Heinrich MIPS book
   should be defined here, but are currently unsupported: .alias,
   .galive, .gjaldef, .gjrlive, .livereg, .noalias.

   The following pseudo-ops from the Kane and Heinrich MIPS book are
   specific to the type of debugging information being generated, and
   should be defined by the object format: .aent, .begin, .bend,
   .bgnb, .end, .endb, .ent, .fmask, .frame, .loc, .mask, .verstamp,
   .vreg.

   The following pseudo-ops from the Kane and Heinrich MIPS book are
   not MIPS CPU specific, but are also not specific to the object file
   format.  This file is probably the best place to define them, but
   they are not currently supported: .asm0, .endr, .lab, .repeat,
   .struct.  */

static const pseudo_typeS mips_pseudo_table[] =
{
  /* MIPS specific pseudo-ops.  */
  {"option", s_option, 0},
  {"set", s_mipsset, 0},
  {"rdata", s_change_sec, 'r'},
  {"sdata", s_change_sec, 's'},
  {"livereg", s_ignore, 0},
  {"abicalls", s_abicalls, 0},
  {"cpload", s_cpload, 0},
  {"cpsetup", s_cpsetup, 0},
  {"cplocal", s_cplocal, 0},
  {"cprestore", s_cprestore, 0},
  {"cpreturn", s_cpreturn, 0},
  {"gpvalue", s_gpvalue, 0},
  {"gpword", s_gpword, 0},
  {"gpdword", s_gpdword, 0},
  {"cpadd", s_cpadd, 0},
  {"insn", s_insn, 0},

  /* Relatively generic pseudo-ops that happen to be used on MIPS
     chips.  */
  {"asciiz", stringer, 1},
  {"bss", s_change_sec, 'b'},
  {"err", s_err, 0},
  {"half", s_cons, 1},
  {"dword", s_cons, 3},
  {"weakext", s_mips_weakext, 0},

  /* These pseudo-ops are defined in read.c, but must be overridden
     here for one reason or another.  */
  {"align", s_align, 0},
  {"byte", s_cons, 0},
  {"data", s_change_sec, 'd'},
  {"double", s_float_cons, 'd'},
  {"float", s_float_cons, 'f'},
  {"globl", s_mips_globl, 0},
  {"global", s_mips_globl, 0},
  {"hword", s_cons, 1},
  {"int", s_cons, 2},
  {"long", s_cons, 2},
  {"octa", s_cons, 4},
  {"quad", s_cons, 3},
  {"section", s_change_section, 0},
  {"short", s_cons, 1},
  {"single", s_float_cons, 'f'},
  {"stabn", s_mips_stab, 'n'},
  {"text", s_change_sec, 't'},
  {"word", s_cons, 2},

  { "extern", ecoff_directive_extern, 0},

  { NULL, NULL, 0 },
};

static const pseudo_typeS mips_nonecoff_pseudo_table[] =
{
  /* These pseudo-ops should be defined by the object file format.
     However, a.out doesn't support them, so we have versions here.  */
  {"aent", s_mips_ent, 1},
  {"bgnb", s_ignore, 0},
  {"end", s_mips_end, 0},
  {"endb", s_ignore, 0},
  {"ent", s_mips_ent, 0},
  {"file", s_mips_file, 0},
  {"fmask", s_mips_mask, 'F'},
  {"frame", s_mips_frame, 0},
  {"loc", s_mips_loc, 0},
  {"mask", s_mips_mask, 'R'},
  {"verstamp", s_ignore, 0},
  { NULL, NULL, 0 },
};

extern void pop_insert (const pseudo_typeS *);

void
mips_pop_insert (void)
{
  pop_insert (mips_pseudo_table);
  if (! ECOFF_DEBUGGING)
    pop_insert (mips_nonecoff_pseudo_table);
}

/* Symbols labelling the current insn.  */

struct insn_label_list
{
  struct insn_label_list *next;
  symbolS *label;
};

static struct insn_label_list *insn_labels;
static struct insn_label_list *free_insn_labels;

static void mips_clear_insn_labels (void);

static inline void
mips_clear_insn_labels (void)
{
  register struct insn_label_list **pl;

  for (pl = &free_insn_labels; *pl != NULL; pl = &(*pl)->next)
    ;
  *pl = insn_labels;
  insn_labels = NULL;
}

static char *expr_end;

/* Expressions which appear in instructions.  These are set by
   mips_ip.  */

static expressionS imm_expr;
static expressionS imm2_expr;
static expressionS offset_expr;

/* Relocs associated with imm_expr and offset_expr.  */

static bfd_reloc_code_real_type imm_reloc[3]
  = {BFD_RELOC_UNUSED, BFD_RELOC_UNUSED, BFD_RELOC_UNUSED};
static bfd_reloc_code_real_type offset_reloc[3]
  = {BFD_RELOC_UNUSED, BFD_RELOC_UNUSED, BFD_RELOC_UNUSED};

/* These are set by mips16_ip if an explicit extension is used.  */

static bfd_boolean mips16_small, mips16_ext;

#ifdef OBJ_ELF
/* The pdr segment for per procedure frame/regmask info.  Not used for
   ECOFF debugging.  */

static segT pdr_seg;
#endif

/* The default target format to use.  */

const char *
mips_target_format (void)
{
  switch (OUTPUT_FLAVOR)
    {
    case bfd_target_aout_flavour:
      return target_big_endian ? "a.out-mips-big" : "a.out-mips-little";
    case bfd_target_ecoff_flavour:
      return target_big_endian ? "ecoff-bigmips" : ECOFF_LITTLE_FORMAT;
    case bfd_target_coff_flavour:
      return "pe-mips";
    case bfd_target_elf_flavour:
#ifdef TE_TMIPS
      /* This is traditional mips.  */
      return (target_big_endian
	      ? (HAVE_64BIT_OBJECTS
		 ? "elf64-tradbigmips"
		 : (HAVE_NEWABI
		    ? "elf32-ntradbigmips" : "elf32-tradbigmips"))
	      : (HAVE_64BIT_OBJECTS
		 ? "elf64-tradlittlemips"
		 : (HAVE_NEWABI
		    ? "elf32-ntradlittlemips" : "elf32-tradlittlemips")));
#else
      return (target_big_endian
	      ? (HAVE_64BIT_OBJECTS
		 ? "elf64-bigmips"
		 : (HAVE_NEWABI
		    ? "elf32-nbigmips" : "elf32-bigmips"))
	      : (HAVE_64BIT_OBJECTS
		 ? "elf64-littlemips"
		 : (HAVE_NEWABI
		    ? "elf32-nlittlemips" : "elf32-littlemips")));
#endif
    default:
      abort ();
      return NULL;
    }
}

/* This function is called once, at assembler startup time.  It should
   set up all the tables, etc. that the MD part of the assembler will need.  */

void
md_begin (void)
{
  register const char *retval = NULL;
  int i = 0;
  int broken = 0;

  if (! bfd_set_arch_mach (stdoutput, bfd_arch_mips, file_mips_arch))
    as_warn (_("Could not set architecture and machine"));

  op_hash = hash_new ();

  for (i = 0; i < NUMOPCODES;)
    {
      const char *name = mips_opcodes[i].name;

      retval = hash_insert (op_hash, name, (void *) &mips_opcodes[i]);
      if (retval != NULL)
	{
	  fprintf (stderr, _("internal error: can't hash `%s': %s\n"),
		   mips_opcodes[i].name, retval);
	  /* Probably a memory allocation problem?  Give up now.  */
	  as_fatal (_("Broken assembler.  No assembly attempted."));
	}
      do
	{
	  if (mips_opcodes[i].pinfo != INSN_MACRO)
	    {
	      if (!validate_mips_insn (&mips_opcodes[i]))
		broken = 1;
	    }
	  ++i;
	}
      while ((i < NUMOPCODES) && !strcmp (mips_opcodes[i].name, name));
    }

  mips16_op_hash = hash_new ();

  i = 0;
  while (i < bfd_mips16_num_opcodes)
    {
      const char *name = mips16_opcodes[i].name;

      retval = hash_insert (mips16_op_hash, name, (void *) &mips16_opcodes[i]);
      if (retval != NULL)
	as_fatal (_("internal: can't hash `%s': %s"),
		  mips16_opcodes[i].name, retval);
      do
	{
	  if (mips16_opcodes[i].pinfo != INSN_MACRO
	      && ((mips16_opcodes[i].match & mips16_opcodes[i].mask)
		  != mips16_opcodes[i].match))
	    {
	      fprintf (stderr, _("internal error: bad mips16 opcode: %s %s\n"),
		       mips16_opcodes[i].name, mips16_opcodes[i].args);
	      broken = 1;
	    }
	  ++i;
	}
      while (i < bfd_mips16_num_opcodes
	     && strcmp (mips16_opcodes[i].name, name) == 0);
    }

  if (broken)
    as_fatal (_("Broken assembler.  No assembly attempted."));

  /* We add all the general register names to the symbol table.  This
     helps us detect invalid uses of them.  */
  for (i = 0; i < 32; i++)
    {
      char buf[5];

      sprintf (buf, "$%d", i);
      symbol_table_insert (symbol_new (buf, reg_section, i,
				       &zero_address_frag));
    }
  symbol_table_insert (symbol_new ("$ra", reg_section, RA,
				   &zero_address_frag));
  symbol_table_insert (symbol_new ("$fp", reg_section, FP,
				   &zero_address_frag));
  symbol_table_insert (symbol_new ("$sp", reg_section, SP,
				   &zero_address_frag));
  symbol_table_insert (symbol_new ("$gp", reg_section, GP,
				   &zero_address_frag));
  symbol_table_insert (symbol_new ("$at", reg_section, AT,
				   &zero_address_frag));
  symbol_table_insert (symbol_new ("$kt0", reg_section, KT0,
				   &zero_address_frag));
  symbol_table_insert (symbol_new ("$kt1", reg_section, KT1,
				   &zero_address_frag));
  symbol_table_insert (symbol_new ("$zero", reg_section, ZERO,
				   &zero_address_frag));
  symbol_table_insert (symbol_new ("$pc", reg_section, -1,
				   &zero_address_frag));

  /* If we don't add these register names to the symbol table, they
     may end up being added as regular symbols by operand(), and then
     make it to the object file as undefined in case they're not
     regarded as local symbols.  They're local in o32, since `$' is a
     local symbol prefix, but not in n32 or n64.  */
  for (i = 0; i < 8; i++)
    {
      char buf[6];

      sprintf (buf, "$fcc%i", i);
      symbol_table_insert (symbol_new (buf, reg_section, -1,
				       &zero_address_frag));
    }

  mips_no_prev_insn (FALSE);

  mips_gprmask = 0;
  mips_cprmask[0] = 0;
  mips_cprmask[1] = 0;
  mips_cprmask[2] = 0;
  mips_cprmask[3] = 0;

  /* set the default alignment for the text section (2**2) */
  record_alignment (text_section, 2);

  if (USE_GLOBAL_POINTER_OPT)
    bfd_set_gp_size (stdoutput, g_switch_value);

  if (OUTPUT_FLAVOR == bfd_target_elf_flavour)
    {
      /* On a native system, sections must be aligned to 16 byte
	 boundaries.  When configured for an embedded ELF target, we
	 don't bother.  */
      if (strcmp (TARGET_OS, "elf") != 0)
	{
	  (void) bfd_set_section_alignment (stdoutput, text_section, 4);
	  (void) bfd_set_section_alignment (stdoutput, data_section, 4);
	  (void) bfd_set_section_alignment (stdoutput, bss_section, 4);
	}

      /* Create a .reginfo section for register masks and a .mdebug
	 section for debugging information.  */
      {
	segT seg;
	subsegT subseg;
	flagword flags;
	segT sec;

	seg = now_seg;
	subseg = now_subseg;

	/* The ABI says this section should be loaded so that the
	   running program can access it.  However, we don't load it
	   if we are configured for an embedded target */
	flags = SEC_READONLY | SEC_DATA;
	if (strcmp (TARGET_OS, "elf") != 0)
	  flags |= SEC_ALLOC | SEC_LOAD;

	if (mips_abi != N64_ABI)
	  {
	    sec = subseg_new (".reginfo", (subsegT) 0);

	    bfd_set_section_flags (stdoutput, sec, flags);
	    bfd_set_section_alignment (stdoutput, sec, HAVE_NEWABI ? 3 : 2);

#ifdef OBJ_ELF
	    mips_regmask_frag = frag_more (sizeof (Elf32_External_RegInfo));
#endif
	  }
	else
	  {
	    /* The 64-bit ABI uses a .MIPS.options section rather than
               .reginfo section.  */
	    sec = subseg_new (".MIPS.options", (subsegT) 0);
	    bfd_set_section_flags (stdoutput, sec, flags);
	    bfd_set_section_alignment (stdoutput, sec, 3);

#ifdef OBJ_ELF
	    /* Set up the option header.  */
	    {
	      Elf_Internal_Options opthdr;
	      char *f;

	      opthdr.kind = ODK_REGINFO;
	      opthdr.size = (sizeof (Elf_External_Options)
			     + sizeof (Elf64_External_RegInfo));
	      opthdr.section = 0;
	      opthdr.info = 0;
	      f = frag_more (sizeof (Elf_External_Options));
	      bfd_mips_elf_swap_options_out (stdoutput, &opthdr,
					     (Elf_External_Options *) f);

	      mips_regmask_frag = frag_more (sizeof (Elf64_External_RegInfo));
	    }
#endif
	  }

	if (ECOFF_DEBUGGING)
	  {
	    sec = subseg_new (".mdebug", (subsegT) 0);
	    (void) bfd_set_section_flags (stdoutput, sec,
					  SEC_HAS_CONTENTS | SEC_READONLY);
	    (void) bfd_set_section_alignment (stdoutput, sec, 2);
	  }
#ifdef OBJ_ELF
	else if (OUTPUT_FLAVOR == bfd_target_elf_flavour && mips_flag_pdr)
	  {
	    pdr_seg = subseg_new (".pdr", (subsegT) 0);
	    (void) bfd_set_section_flags (stdoutput, pdr_seg,
					  SEC_READONLY | SEC_RELOC
					  | SEC_DEBUGGING);
	    (void) bfd_set_section_alignment (stdoutput, pdr_seg, 2);
	  }
#endif

	subseg_set (seg, subseg);
      }
    }

  if (! ECOFF_DEBUGGING)
    md_obj_begin ();
}

void
md_mips_end (void)
{
  if (! ECOFF_DEBUGGING)
    md_obj_end ();
}

void
md_assemble (char *str)
{
  struct mips_cl_insn insn;
  bfd_reloc_code_real_type unused_reloc[3]
    = {BFD_RELOC_UNUSED, BFD_RELOC_UNUSED, BFD_RELOC_UNUSED};

  imm_expr.X_op = O_absent;
  imm2_expr.X_op = O_absent;
  offset_expr.X_op = O_absent;
  imm_reloc[0] = BFD_RELOC_UNUSED;
  imm_reloc[1] = BFD_RELOC_UNUSED;
  imm_reloc[2] = BFD_RELOC_UNUSED;
  offset_reloc[0] = BFD_RELOC_UNUSED;
  offset_reloc[1] = BFD_RELOC_UNUSED;
  offset_reloc[2] = BFD_RELOC_UNUSED;

  if (mips_opts.mips16)
    mips16_ip (str, &insn);
  else
    {
      mips_ip (str, &insn);
      DBG ((_("returned from mips_ip(%s) insn_opcode = 0x%x\n"),
	    str, insn.insn_opcode));
    }

  if (insn_error)
    {
      as_bad ("%s `%s'", insn_error, str);
      return;
    }

  if (insn.insn_mo->pinfo == INSN_MACRO)
    {
      macro_start ();
      if (mips_opts.mips16)
	mips16_macro (&insn);
      else
	macro (&insn);
      macro_end ();
    }
  else
    {
      if (imm_expr.X_op != O_absent)
	append_insn (&insn, &imm_expr, imm_reloc);
      else if (offset_expr.X_op != O_absent)
	append_insn (&insn, &offset_expr, offset_reloc);
      else
	append_insn (&insn, NULL, unused_reloc);
    }
}

/* Return true if the given relocation might need a matching %lo().
   Note that R_MIPS_GOT16 relocations only need a matching %lo() when
   applied to local symbols.  */

static inline bfd_boolean
reloc_needs_lo_p (bfd_reloc_code_real_type reloc)
{
  return (reloc == BFD_RELOC_HI16_S
	  || reloc == BFD_RELOC_MIPS_GOT16);
}

/* Return true if the given fixup is followed by a matching R_MIPS_LO16
   relocation.  */

static inline bfd_boolean
fixup_has_matching_lo_p (fixS *fixp)
{
  return (fixp->fx_next != NULL
	  && fixp->fx_next->fx_r_type == BFD_RELOC_LO16
	  && fixp->fx_addsy == fixp->fx_next->fx_addsy
	  && fixp->fx_offset == fixp->fx_next->fx_offset);
}

/* See whether instruction IP reads register REG.  CLASS is the type
   of register.  */

static int
insn_uses_reg (struct mips_cl_insn *ip, unsigned int reg,
	       enum mips_regclass class)
{
  if (class == MIPS16_REG)
    {
      assert (mips_opts.mips16);
      reg = mips16_to_32_reg_map[reg];
      class = MIPS_GR_REG;
    }

  /* Don't report on general register ZERO, since it never changes.  */
  if (class == MIPS_GR_REG && reg == ZERO)
    return 0;

  if (class == MIPS_FP_REG)
    {
      assert (! mips_opts.mips16);
      /* If we are called with either $f0 or $f1, we must check $f0.
	 This is not optimal, because it will introduce an unnecessary
	 NOP between "lwc1 $f0" and "swc1 $f1".  To fix this we would
	 need to distinguish reading both $f0 and $f1 or just one of
	 them.  Note that we don't have to check the other way,
	 because there is no instruction that sets both $f0 and $f1
	 and requires a delay.  */
      if ((ip->insn_mo->pinfo & INSN_READ_FPR_S)
	  && ((((ip->insn_opcode >> OP_SH_FS) & OP_MASK_FS) &~(unsigned)1)
	      == (reg &~ (unsigned) 1)))
	return 1;
      if ((ip->insn_mo->pinfo & INSN_READ_FPR_T)
	  && ((((ip->insn_opcode >> OP_SH_FT) & OP_MASK_FT) &~(unsigned)1)
	      == (reg &~ (unsigned) 1)))
	return 1;
    }
  else if (! mips_opts.mips16)
    {
      if ((ip->insn_mo->pinfo & INSN_READ_GPR_S)
	  && ((ip->insn_opcode >> OP_SH_RS) & OP_MASK_RS) == reg)
	return 1;
      if ((ip->insn_mo->pinfo & INSN_READ_GPR_T)
	  && ((ip->insn_opcode >> OP_SH_RT) & OP_MASK_RT) == reg)
	return 1;
    }
  else
    {
      if ((ip->insn_mo->pinfo & MIPS16_INSN_READ_X)
	  && (mips16_to_32_reg_map[((ip->insn_opcode >> MIPS16OP_SH_RX)
				    & MIPS16OP_MASK_RX)]
	      == reg))
	return 1;
      if ((ip->insn_mo->pinfo & MIPS16_INSN_READ_Y)
	  && (mips16_to_32_reg_map[((ip->insn_opcode >> MIPS16OP_SH_RY)
				    & MIPS16OP_MASK_RY)]
	      == reg))
	return 1;
      if ((ip->insn_mo->pinfo & MIPS16_INSN_READ_Z)
	  && (mips16_to_32_reg_map[((ip->insn_opcode >> MIPS16OP_SH_MOVE32Z)
				    & MIPS16OP_MASK_MOVE32Z)]
	      == reg))
	return 1;
      if ((ip->insn_mo->pinfo & MIPS16_INSN_READ_T) && reg == TREG)
	return 1;
      if ((ip->insn_mo->pinfo & MIPS16_INSN_READ_SP) && reg == SP)
	return 1;
      if ((ip->insn_mo->pinfo & MIPS16_INSN_READ_31) && reg == RA)
	return 1;
      if ((ip->insn_mo->pinfo & MIPS16_INSN_READ_GPR_X)
	  && ((ip->insn_opcode >> MIPS16OP_SH_REGR32)
	      & MIPS16OP_MASK_REGR32) == reg)
	return 1;
    }

  return 0;
}

/* This function returns true if modifying a register requires a
   delay.  */

static int
reg_needs_delay (unsigned int reg)
{
  unsigned long prev_pinfo;

  prev_pinfo = prev_insn.insn_mo->pinfo;
  if (! mips_opts.noreorder
      && (((prev_pinfo & INSN_LOAD_MEMORY_DELAY)
	   && ! gpr_interlocks)
	  || ((prev_pinfo & INSN_LOAD_COPROC_DELAY)
	      && ! cop_interlocks)))
    {
      /* A load from a coprocessor or from memory.  All load delays
	 delay the use of general register rt for one instruction.  */
      /* Itbl support may require additional care here.  */
      know (prev_pinfo & INSN_WRITE_GPR_T);
      if (reg == ((prev_insn.insn_opcode >> OP_SH_RT) & OP_MASK_RT))
	return 1;
    }

  return 0;
}

/* Mark instruction labels in mips16 mode.  This permits the linker to
   handle them specially, such as generating jalx instructions when
   needed.  We also make them odd for the duration of the assembly, in
   order to generate the right sort of code.  We will make them even
   in the adjust_symtab routine, while leaving them marked.  This is
   convenient for the debugger and the disassembler.  The linker knows
   to make them odd again.  */

static void
mips16_mark_labels (void)
{
  if (mips_opts.mips16)
    {
      struct insn_label_list *l;
      valueT val;

      for (l = insn_labels; l != NULL; l = l->next)
	{
#ifdef OBJ_ELF
	  if (OUTPUT_FLAVOR == bfd_target_elf_flavour)
	    S_SET_OTHER (l->label, STO_MIPS16);
#endif
	  val = S_GET_VALUE (l->label);
	  if ((val & 1) == 0)
	    S_SET_VALUE (l->label, val + 1);
	}
    }
}

/* End the current frag.  Make it a variant frag and record the
   relaxation info.  */

static void
relax_close_frag (void)
{
  mips_macro_warning.first_frag = frag_now;
  frag_var (rs_machine_dependent, 0, 0,
	    RELAX_ENCODE (mips_relax.sizes[0], mips_relax.sizes[1]),
	    mips_relax.symbol, 0, (char *) mips_relax.first_fixup);

  memset (&mips_relax.sizes, 0, sizeof (mips_relax.sizes));
  mips_relax.first_fixup = 0;
}

/* Start a new relaxation sequence whose expansion depends on SYMBOL.
   See the comment above RELAX_ENCODE for more details.  */

static void
relax_start (symbolS *symbol)
{
  assert (mips_relax.sequence == 0);
  mips_relax.sequence = 1;
  mips_relax.symbol = symbol;
}

/* Start generating the second version of a relaxable sequence.
   See the comment above RELAX_ENCODE for more details.  */

static void
relax_switch (void)
{
  assert (mips_relax.sequence == 1);
  mips_relax.sequence = 2;
}

/* End the current relaxable sequence.  */

static void
relax_end (void)
{
  assert (mips_relax.sequence == 2);
  relax_close_frag ();
  mips_relax.sequence = 0;
}

/* Output an instruction.  IP is the instruction information.
   ADDRESS_EXPR is an operand of the instruction to be used with
   RELOC_TYPE.  */

static void
append_insn (struct mips_cl_insn *ip, expressionS *address_expr,
	     bfd_reloc_code_real_type *reloc_type)
{
  register unsigned long prev_pinfo, pinfo;
  char *f;
  fixS *fixp[3];
  int nops = 0;
  relax_stateT prev_insn_frag_type = 0;
  bfd_boolean relaxed_branch = FALSE;
  bfd_boolean force_new_frag = FALSE;

  /* Mark instruction labels in mips16 mode.  */
  mips16_mark_labels ();

  prev_pinfo = prev_insn.insn_mo->pinfo;
  pinfo = ip->insn_mo->pinfo;

  if (mips_relax.sequence != 2
      && (!mips_opts.noreorder || prev_nop_frag != NULL))
    {
      int prev_prev_nop;

      /* If the previous insn required any delay slots, see if we need
	 to insert a NOP or two.  There are eight kinds of possible
	 hazards, of which an instruction can have at most one type.
	 (1) a load from memory delay
	 (2) a load from a coprocessor delay
	 (3) an unconditional branch delay
	 (4) a conditional branch delay
	 (5) a move to coprocessor register delay
	 (6) a load coprocessor register from memory delay
	 (7) a coprocessor condition code delay
	 (8) a HI/LO special register delay

	 There are a lot of optimizations we could do that we don't.
	 In particular, we do not, in general, reorder instructions.
	 If you use gcc with optimization, it will reorder
	 instructions and generally do much more optimization then we
	 do here; repeating all that work in the assembler would only
	 benefit hand written assembly code, and does not seem worth
	 it.  */

      /* This is how a NOP is emitted.  */
#define emit_nop()					\
  (mips_opts.mips16					\
   ? md_number_to_chars (frag_more (2), 0x6500, 2)	\
   : md_number_to_chars (frag_more (4), 0, 4))

      /* The previous insn might require a delay slot, depending upon
	 the contents of the current insn.  */
      if (! mips_opts.mips16
	  && (((prev_pinfo & INSN_LOAD_MEMORY_DELAY)
	       && ! gpr_interlocks)
	      || ((prev_pinfo & INSN_LOAD_COPROC_DELAY)
		  && ! cop_interlocks)))
	{
	  /* A load from a coprocessor or from memory.  All load
	     delays delay the use of general register rt for one
	     instruction.  */
	  /* Itbl support may require additional care here.  */
	  know (prev_pinfo & INSN_WRITE_GPR_T);
	  if (mips_optimize == 0
	      || insn_uses_reg (ip,
				((prev_insn.insn_opcode >> OP_SH_RT)
				 & OP_MASK_RT),
				MIPS_GR_REG))
	    ++nops;
	}
      else if (! mips_opts.mips16
	       && (((prev_pinfo & INSN_COPROC_MOVE_DELAY)
		    && ! cop_interlocks)
		   || ((prev_pinfo & INSN_COPROC_MEMORY_DELAY)
		       && ! cop_mem_interlocks)))
	{
	  /* A generic coprocessor delay.  The previous instruction
	     modified a coprocessor general or control register.  If
	     it modified a control register, we need to avoid any
	     coprocessor instruction (this is probably not always
	     required, but it sometimes is).  If it modified a general
	     register, we avoid using that register.

	     This case is not handled very well.  There is no special
	     knowledge of CP0 handling, and the coprocessors other
	     than the floating point unit are not distinguished at
	     all.  */
          /* Itbl support may require additional care here. FIXME!
             Need to modify this to include knowledge about
             user specified delays!  */
	  if (prev_pinfo & INSN_WRITE_FPR_T)
	    {
	      if (mips_optimize == 0
		  || insn_uses_reg (ip,
				    ((prev_insn.insn_opcode >> OP_SH_FT)
				     & OP_MASK_FT),
				    MIPS_FP_REG))
		++nops;
	    }
	  else if (prev_pinfo & INSN_WRITE_FPR_S)
	    {
	      if (mips_optimize == 0
		  || insn_uses_reg (ip,
				    ((prev_insn.insn_opcode >> OP_SH_FS)
				     & OP_MASK_FS),
				    MIPS_FP_REG))
		++nops;
	    }
	  else
	    {
	      /* We don't know exactly what the previous instruction
		 does.  If the current instruction uses a coprocessor
		 register, we must insert a NOP.  If previous
		 instruction may set the condition codes, and the
		 current instruction uses them, we must insert two
		 NOPS.  */
              /* Itbl support may require additional care here.  */
	      if (mips_optimize == 0
		  || ((prev_pinfo & INSN_WRITE_COND_CODE)
		      && (pinfo & INSN_READ_COND_CODE)))
		nops += 2;
	      else if (pinfo & INSN_COP)
		++nops;
	    }
	}
      else if (! mips_opts.mips16
	       && (prev_pinfo & INSN_WRITE_COND_CODE)
               && ! cop_interlocks)
	{
	  /* The previous instruction sets the coprocessor condition
	     codes, but does not require a general coprocessor delay
	     (this means it is a floating point comparison
	     instruction).  If this instruction uses the condition
	     codes, we need to insert a single NOP.  */
	  /* Itbl support may require additional care here.  */
	  if (mips_optimize == 0
	      || (pinfo & INSN_READ_COND_CODE))
	    ++nops;
	}

      /* If we're fixing up mfhi/mflo for the r7000 and the
	 previous insn was an mfhi/mflo and the current insn
	 reads the register that the mfhi/mflo wrote to, then
	 insert two nops.  */

      else if (mips_7000_hilo_fix
	       && MF_HILO_INSN (prev_pinfo)
	       && insn_uses_reg (ip, ((prev_insn.insn_opcode >> OP_SH_RD)
				      & OP_MASK_RD),
				 MIPS_GR_REG))
	{
	  nops += 2;
	}

      /* If we're fixing up mfhi/mflo for the r7000 and the
	 2nd previous insn was an mfhi/mflo and the current insn
	 reads the register that the mfhi/mflo wrote to, then
	 insert one nop.  */

      else if (mips_7000_hilo_fix
	       && MF_HILO_INSN (prev_prev_insn.insn_opcode)
	       && insn_uses_reg (ip, ((prev_prev_insn.insn_opcode >> OP_SH_RD)
                                       & OP_MASK_RD),
                                    MIPS_GR_REG))

	{
	  ++nops;
	}

      else if (prev_pinfo & INSN_READ_LO)
	{
	  /* The previous instruction reads the LO register; if the
	     current instruction writes to the LO register, we must
	     insert two NOPS.  Some newer processors have interlocks.
	     Also the tx39's multiply instructions can be executed
             immediately after a read from HI/LO (without the delay),
             though the tx39's divide insns still do require the
	     delay.  */
	  if (! (hilo_interlocks
		 || (mips_opts.arch == CPU_R3900 && (pinfo & INSN_MULT)))
	      && (mips_optimize == 0
		  || (pinfo & INSN_WRITE_LO)))
	    nops += 2;
	  /* Most mips16 branch insns don't have a delay slot.
	     If a read from LO is immediately followed by a branch
	     to a write to LO we have a read followed by a write
	     less than 2 insns away.  We assume the target of
	     a branch might be a write to LO, and insert a nop
	     between a read and an immediately following branch.  */
	  else if (mips_opts.mips16
		   && (mips_optimize == 0
		       || (pinfo & MIPS16_INSN_BRANCH)))
	    ++nops;
	}
      else if (prev_insn.insn_mo->pinfo & INSN_READ_HI)
	{
	  /* The previous instruction reads the HI register; if the
	     current instruction writes to the HI register, we must
	     insert a NOP.  Some newer processors have interlocks.
	     Also the note tx39's multiply above.  */
	  if (! (hilo_interlocks
		 || (mips_opts.arch == CPU_R3900 && (pinfo & INSN_MULT)))
	      && (mips_optimize == 0
		  || (pinfo & INSN_WRITE_HI)))
	    nops += 2;
	  /* Most mips16 branch insns don't have a delay slot.
	     If a read from HI is immediately followed by a branch
	     to a write to HI we have a read followed by a write
	     less than 2 insns away.  We assume the target of
	     a branch might be a write to HI, and insert a nop
	     between a read and an immediately following branch.  */
	  else if (mips_opts.mips16
		   && (mips_optimize == 0
		       || (pinfo & MIPS16_INSN_BRANCH)))
	    ++nops;
	}

      /* If the previous instruction was in a noreorder section, then
         we don't want to insert the nop after all.  */
      /* Itbl support may require additional care here.  */
      if (prev_insn_unreordered)
	nops = 0;

      /* There are two cases which require two intervening
	 instructions: 1) setting the condition codes using a move to
	 coprocessor instruction which requires a general coprocessor
	 delay and then reading the condition codes 2) reading the HI
	 or LO register and then writing to it (except on processors
	 which have interlocks).  If we are not already emitting a NOP
	 instruction, we must check for these cases compared to the
	 instruction previous to the previous instruction.  */
      if ((! mips_opts.mips16
	   && (prev_prev_insn.insn_mo->pinfo & INSN_COPROC_MOVE_DELAY)
	   && (prev_prev_insn.insn_mo->pinfo & INSN_WRITE_COND_CODE)
	   && (pinfo & INSN_READ_COND_CODE)
	   && ! cop_interlocks)
	  || ((prev_prev_insn.insn_mo->pinfo & INSN_READ_LO)
	      && (pinfo & INSN_WRITE_LO)
	      && ! (hilo_interlocks
		    || (mips_opts.arch == CPU_R3900 && (pinfo & INSN_MULT))))
	  || ((prev_prev_insn.insn_mo->pinfo & INSN_READ_HI)
	      && (pinfo & INSN_WRITE_HI)
	      && ! (hilo_interlocks
		    || (mips_opts.arch == CPU_R3900 && (pinfo & INSN_MULT)))))
	prev_prev_nop = 1;
      else
	prev_prev_nop = 0;

      if (prev_prev_insn_unreordered)
	prev_prev_nop = 0;

      if (prev_prev_nop && nops == 0)
	++nops;

      if (mips_fix_4122_bugs && prev_insn.insn_mo->name)
	{
	  /* We're out of bits in pinfo, so we must resort to string
	     ops here.  Shortcuts are selected based on opcodes being
	     limited to the VR4122 instruction set.  */
	  int min_nops = 0;
	  const char *pn = prev_insn.insn_mo->name;
	  const char *tn = ip->insn_mo->name;
	  if (strncmp(pn, "macc", 4) == 0
	      || strncmp(pn, "dmacc", 5) == 0)
	    {
	      /* Errata 21 - [D]DIV[U] after [D]MACC */
	      if (strstr (tn, "div"))
		{
		  min_nops = 1;
		}

	      /* Errata 23 - Continuous DMULT[U]/DMACC instructions */
	      if (pn[0] == 'd' /* dmacc */
		  && (strncmp(tn, "dmult", 5) == 0
		      || strncmp(tn, "dmacc", 5) == 0))
		{
		  min_nops = 1;
		}

	      /* Errata 24 - MT{LO,HI} after [D]MACC */
	      if (strcmp (tn, "mtlo") == 0
		  || strcmp (tn, "mthi") == 0)
		{
		  min_nops = 1;
		}

	    }
	  else if (strncmp(pn, "dmult", 5) == 0
		   && (strncmp(tn, "dmult", 5) == 0
		       || strncmp(tn, "dmacc", 5) == 0))
	    {
	      /* Here is the rest of errata 23.  */
	      min_nops = 1;
	    }
	  if (nops < min_nops)
	    nops = min_nops;
	}

      /* If we are being given a nop instruction, don't bother with
	 one of the nops we would otherwise output.  This will only
	 happen when a nop instruction is used with mips_optimize set
	 to 0.  */
      if (nops > 0
	  && ! mips_opts.noreorder
	  && ip->insn_opcode == (unsigned) (mips_opts.mips16 ? 0x6500 : 0))
	--nops;

      /* Now emit the right number of NOP instructions.  */
      if (nops > 0 && ! mips_opts.noreorder)
	{
	  fragS *old_frag;
	  unsigned long old_frag_offset;
	  int i;
	  struct insn_label_list *l;

	  old_frag = frag_now;
	  old_frag_offset = frag_now_fix ();

	  for (i = 0; i < nops; i++)
	    emit_nop ();

	  if (listing)
	    {
	      listing_prev_line ();
	      /* We may be at the start of a variant frag.  In case we
                 are, make sure there is enough space for the frag
                 after the frags created by listing_prev_line.  The
                 argument to frag_grow here must be at least as large
                 as the argument to all other calls to frag_grow in
                 this file.  We don't have to worry about being in the
                 middle of a variant frag, because the variants insert
                 all needed nop instructions themselves.  */
	      frag_grow (40);
	    }

	  for (l = insn_labels; l != NULL; l = l->next)
	    {
	      valueT val;

	      assert (S_GET_SEGMENT (l->label) == now_seg);
	      symbol_set_frag (l->label, frag_now);
	      val = (valueT) frag_now_fix ();
	      /* mips16 text labels are stored as odd.  */
	      if (mips_opts.mips16)
		++val;
	      S_SET_VALUE (l->label, val);
	    }

#ifndef NO_ECOFF_DEBUGGING
	  if (ECOFF_DEBUGGING)
	    ecoff_fix_loc (old_frag, old_frag_offset);
#endif
	}
      else if (prev_nop_frag != NULL)
	{
	  /* We have a frag holding nops we may be able to remove.  If
             we don't need any nops, we can decrease the size of
             prev_nop_frag by the size of one instruction.  If we do
             need some nops, we count them in prev_nops_required.  */
	  if (prev_nop_frag_since == 0)
	    {
	      if (nops == 0)
		{
		  prev_nop_frag->fr_fix -= mips_opts.mips16 ? 2 : 4;
		  --prev_nop_frag_holds;
		}
	      else
		prev_nop_frag_required += nops;
	    }
	  else
	    {
	      if (prev_prev_nop == 0)
		{
		  prev_nop_frag->fr_fix -= mips_opts.mips16 ? 2 : 4;
		  --prev_nop_frag_holds;
		}
	      else
		++prev_nop_frag_required;
	    }

	  if (prev_nop_frag_holds <= prev_nop_frag_required)
	    prev_nop_frag = NULL;

	  ++prev_nop_frag_since;

	  /* Sanity check: by the time we reach the second instruction
             after prev_nop_frag, we should have used up all the nops
             one way or another.  */
	  assert (prev_nop_frag_since <= 1 || prev_nop_frag == NULL);
	}
    }

  /* Record the frag type before frag_var.  */
  if (prev_insn_frag)
    prev_insn_frag_type = prev_insn_frag->fr_type;

  if (address_expr
      && *reloc_type == BFD_RELOC_16_PCREL_S2
      && (pinfo & INSN_UNCOND_BRANCH_DELAY || pinfo & INSN_COND_BRANCH_DELAY
	  || pinfo & INSN_COND_BRANCH_LIKELY)
      && mips_relax_branch
      /* Don't try branch relaxation within .set nomacro, or within
	 .set noat if we use $at for PIC computations.  If it turns
	 out that the branch was out-of-range, we'll get an error.  */
      && !mips_opts.warn_about_macros
      && !(mips_opts.noat && mips_pic != NO_PIC)
      && !mips_opts.mips16)
    {
      relaxed_branch = TRUE;
      f = frag_var (rs_machine_dependent,
		    relaxed_branch_length
		    (NULL, NULL,
		     (pinfo & INSN_UNCOND_BRANCH_DELAY) ? -1
		     : (pinfo & INSN_COND_BRANCH_LIKELY) ? 1 : 0), 4,
		    RELAX_BRANCH_ENCODE
		    (pinfo & INSN_UNCOND_BRANCH_DELAY,
		     pinfo & INSN_COND_BRANCH_LIKELY,
		     pinfo & INSN_WRITE_GPR_31,
		     0),
		    address_expr->X_add_symbol,
		    address_expr->X_add_number,
		    0);
      *reloc_type = BFD_RELOC_UNUSED;
    }
  else if (*reloc_type > BFD_RELOC_UNUSED)
    {
      /* We need to set up a variant frag.  */
      assert (mips_opts.mips16 && address_expr != NULL);
      f = frag_var (rs_machine_dependent, 4, 0,
		    RELAX_MIPS16_ENCODE (*reloc_type - BFD_RELOC_UNUSED,
					 mips16_small, mips16_ext,
					 (prev_pinfo
					  & INSN_UNCOND_BRANCH_DELAY),
					 (*prev_insn_reloc_type
					  == BFD_RELOC_MIPS16_JMP)),
		    make_expr_symbol (address_expr), 0, NULL);
    }
  else if (mips_opts.mips16
	   && ! ip->use_extend
	   && *reloc_type != BFD_RELOC_MIPS16_JMP)
    {
      /* Make sure there is enough room to swap this instruction with
         a following jump instruction.  */
      frag_grow (6);
      f = frag_more (2);
    }
  else
    {
      if (mips_opts.mips16
	  && mips_opts.noreorder
	  && (prev_pinfo & INSN_UNCOND_BRANCH_DELAY) != 0)
	as_warn (_("extended instruction in delay slot"));

      if (mips_relax.sequence)
	{
	  /* If we've reached the end of this frag, turn it into a variant
	     frag and record the information for the instructions we've
	     written so far.  */
	  if (frag_room () < 4)
	    relax_close_frag ();
	  mips_relax.sizes[mips_relax.sequence - 1] += 4;
	}

      if (mips_relax.sequence != 2)
	mips_macro_warning.sizes[0] += 4;
      if (mips_relax.sequence != 1)
	mips_macro_warning.sizes[1] += 4;

      f = frag_more (4);
    }

  fixp[0] = fixp[1] = fixp[2] = NULL;
  if (address_expr != NULL && *reloc_type < BFD_RELOC_UNUSED)
    {
      if (address_expr->X_op == O_constant)
	{
	  valueT tmp;

	  switch (*reloc_type)
	    {
	    case BFD_RELOC_32:
	      ip->insn_opcode |= address_expr->X_add_number;
	      break;

	    case BFD_RELOC_MIPS_HIGHEST:
	      tmp = (address_expr->X_add_number
		     + ((valueT) 0x8000 << 32) + 0x80008000) >> 16;
	      tmp >>= 16;
	      ip->insn_opcode |= (tmp >> 16) & 0xffff;
	      break;

	    case BFD_RELOC_MIPS_HIGHER:
	      tmp = (address_expr->X_add_number + 0x80008000) >> 16;
	      ip->insn_opcode |= (tmp >> 16) & 0xffff;
	      break;

	    case BFD_RELOC_HI16_S:
	      ip->insn_opcode |= ((address_expr->X_add_number + 0x8000)
				  >> 16) & 0xffff;
	      break;

	    case BFD_RELOC_HI16:
	      ip->insn_opcode |= (address_expr->X_add_number >> 16) & 0xffff;
	      break;

	    case BFD_RELOC_LO16:
	    case BFD_RELOC_MIPS_GOT_DISP:
	      ip->insn_opcode |= address_expr->X_add_number & 0xffff;
	      break;

	    case BFD_RELOC_MIPS_JMP:
	      if ((address_expr->X_add_number & 3) != 0)
		as_bad (_("jump to misaligned address (0x%lx)"),
			(unsigned long) address_expr->X_add_number);
	      if (address_expr->X_add_number & ~0xfffffff)
		as_bad (_("jump address range overflow (0x%lx)"),
			(unsigned long) address_expr->X_add_number);
	      ip->insn_opcode |= (address_expr->X_add_number >> 2) & 0x3ffffff;
	      break;

	    case BFD_RELOC_MIPS16_JMP:
	      if ((address_expr->X_add_number & 3) != 0)
		as_bad (_("jump to misaligned address (0x%lx)"),
			(unsigned long) address_expr->X_add_number);
	      if (address_expr->X_add_number & ~0xfffffff)
		as_bad (_("jump address range overflow (0x%lx)"),
			(unsigned long) address_expr->X_add_number);
	      ip->insn_opcode |=
		(((address_expr->X_add_number & 0x7c0000) << 3)
		 | ((address_expr->X_add_number & 0xf800000) >> 7)
		 | ((address_expr->X_add_number & 0x3fffc) >> 2));
	      break;

	    case BFD_RELOC_16_PCREL_S2:
	      goto need_reloc;

	    default:
	      internalError ();
	    }
	}
      else
	need_reloc:
	{
	  reloc_howto_type *howto;
	  int i;

	  /* In a compound relocation, it is the final (outermost)
	     operator that determines the relocated field.  */
	  for (i = 1; i < 3; i++)
	    if (reloc_type[i] == BFD_RELOC_UNUSED)
	      break;

	  howto = bfd_reloc_type_lookup (stdoutput, reloc_type[i - 1]);
	  fixp[0] = fix_new_exp (frag_now, f - frag_now->fr_literal,
				 bfd_get_reloc_size(howto),
				 address_expr,
				 reloc_type[0] == BFD_RELOC_16_PCREL_S2,
				 reloc_type[0]);

	  /* These relocations can have an addend that won't fit in
	     4 octets for 64bit assembly.  */
	  if (HAVE_64BIT_GPRS
	      && ! howto->partial_inplace
	      && (reloc_type[0] == BFD_RELOC_16
		  || reloc_type[0] == BFD_RELOC_32
		  || reloc_type[0] == BFD_RELOC_MIPS_JMP
		  || reloc_type[0] == BFD_RELOC_HI16_S
		  || reloc_type[0] == BFD_RELOC_LO16
		  || reloc_type[0] == BFD_RELOC_GPREL16
		  || reloc_type[0] == BFD_RELOC_MIPS_LITERAL
		  || reloc_type[0] == BFD_RELOC_GPREL32
		  || reloc_type[0] == BFD_RELOC_64
		  || reloc_type[0] == BFD_RELOC_CTOR
		  || reloc_type[0] == BFD_RELOC_MIPS_SUB
		  || reloc_type[0] == BFD_RELOC_MIPS_HIGHEST
		  || reloc_type[0] == BFD_RELOC_MIPS_HIGHER
		  || reloc_type[0] == BFD_RELOC_MIPS_SCN_DISP
		  || reloc_type[0] == BFD_RELOC_MIPS_REL16
		  || reloc_type[0] == BFD_RELOC_MIPS_RELGOT))
	    fixp[0]->fx_no_overflow = 1;

	  if (mips_relax.sequence)
	    {
	      if (mips_relax.first_fixup == 0)
		mips_relax.first_fixup = fixp[0];
	    }
	  else if (reloc_needs_lo_p (*reloc_type))
	    {
	      struct mips_hi_fixup *hi_fixup;

	      /* Reuse the last entry if it already has a matching %lo.  */
	      hi_fixup = mips_hi_fixup_list;
	      if (hi_fixup == 0
		  || !fixup_has_matching_lo_p (hi_fixup->fixp))
		{
		  hi_fixup = ((struct mips_hi_fixup *)
			      xmalloc (sizeof (struct mips_hi_fixup)));
		  hi_fixup->next = mips_hi_fixup_list;
		  mips_hi_fixup_list = hi_fixup;
		}
	      hi_fixup->fixp = fixp[0];
	      hi_fixup->seg = now_seg;
	    }

	  /* Add fixups for the second and third relocations, if given.
	     Note that the ABI allows the second relocation to be
	     against RSS_UNDEF, RSS_GP, RSS_GP0 or RSS_LOC.  At the
	     moment we only use RSS_UNDEF, but we could add support
	     for the others if it ever becomes necessary.  */
	  for (i = 1; i < 3; i++)
	    if (reloc_type[i] != BFD_RELOC_UNUSED)
	      {
		address_expr->X_op = O_absent;
		address_expr->X_add_symbol = 0;
		address_expr->X_add_number = 0;

		fixp[i] = fix_new_exp (frag_now, fixp[0]->fx_where,
				       fixp[0]->fx_size, address_expr,
				       FALSE, reloc_type[i]);
	      }
	}
    }

  if (! mips_opts.mips16)
    {
      md_number_to_chars (f, ip->insn_opcode, 4);
#ifdef OBJ_ELF
      dwarf2_emit_insn (4);
#endif
    }
  else if (*reloc_type == BFD_RELOC_MIPS16_JMP)
    {
      md_number_to_chars (f, ip->insn_opcode >> 16, 2);
      md_number_to_chars (f + 2, ip->insn_opcode & 0xffff, 2);
#ifdef OBJ_ELF
      dwarf2_emit_insn (4);
#endif
    }
  else
    {
      if (ip->use_extend)
	{
	  md_number_to_chars (f, 0xf000 | ip->extend, 2);
	  f += 2;
	}
      md_number_to_chars (f, ip->insn_opcode, 2);
#ifdef OBJ_ELF
      dwarf2_emit_insn (ip->use_extend ? 4 : 2);
#endif
    }

  /* Update the register mask information.  */
  if (! mips_opts.mips16)
    {
      if (pinfo & INSN_WRITE_GPR_D)
	mips_gprmask |= 1 << ((ip->insn_opcode >> OP_SH_RD) & OP_MASK_RD);
      if ((pinfo & (INSN_WRITE_GPR_T | INSN_READ_GPR_T)) != 0)
	mips_gprmask |= 1 << ((ip->insn_opcode >> OP_SH_RT) & OP_MASK_RT);
      if (pinfo & INSN_READ_GPR_S)
	mips_gprmask |= 1 << ((ip->insn_opcode >> OP_SH_RS) & OP_MASK_RS);
      if (pinfo & INSN_WRITE_GPR_31)
	mips_gprmask |= 1 << RA;
      if (pinfo & INSN_WRITE_FPR_D)
	mips_cprmask[1] |= 1 << ((ip->insn_opcode >> OP_SH_FD) & OP_MASK_FD);
      if ((pinfo & (INSN_WRITE_FPR_S | INSN_READ_FPR_S)) != 0)
	mips_cprmask[1] |= 1 << ((ip->insn_opcode >> OP_SH_FS) & OP_MASK_FS);
      if ((pinfo & (INSN_WRITE_FPR_T | INSN_READ_FPR_T)) != 0)
	mips_cprmask[1] |= 1 << ((ip->insn_opcode >> OP_SH_FT) & OP_MASK_FT);
      if ((pinfo & INSN_READ_FPR_R) != 0)
	mips_cprmask[1] |= 1 << ((ip->insn_opcode >> OP_SH_FR) & OP_MASK_FR);
      if (pinfo & INSN_COP)
	{
	  /* We don't keep enough information to sort these cases out.
	     The itbl support does keep this information however, although
	     we currently don't support itbl fprmats as part of the cop
	     instruction.  May want to add this support in the future.  */
	}
      /* Never set the bit for $0, which is always zero.  */
      mips_gprmask &= ~1 << 0;
    }
  else
    {
      if (pinfo & (MIPS16_INSN_WRITE_X | MIPS16_INSN_READ_X))
	mips_gprmask |= 1 << ((ip->insn_opcode >> MIPS16OP_SH_RX)
			      & MIPS16OP_MASK_RX);
      if (pinfo & (MIPS16_INSN_WRITE_Y | MIPS16_INSN_READ_Y))
	mips_gprmask |= 1 << ((ip->insn_opcode >> MIPS16OP_SH_RY)
			      & MIPS16OP_MASK_RY);
      if (pinfo & MIPS16_INSN_WRITE_Z)
	mips_gprmask |= 1 << ((ip->insn_opcode >> MIPS16OP_SH_RZ)
			      & MIPS16OP_MASK_RZ);
      if (pinfo & (MIPS16_INSN_WRITE_T | MIPS16_INSN_READ_T))
	mips_gprmask |= 1 << TREG;
      if (pinfo & (MIPS16_INSN_WRITE_SP | MIPS16_INSN_READ_SP))
	mips_gprmask |= 1 << SP;
      if (pinfo & (MIPS16_INSN_WRITE_31 | MIPS16_INSN_READ_31))
	mips_gprmask |= 1 << RA;
      if (pinfo & MIPS16_INSN_WRITE_GPR_Y)
	mips_gprmask |= 1 << MIPS16OP_EXTRACT_REG32R (ip->insn_opcode);
      if (pinfo & MIPS16_INSN_READ_Z)
	mips_gprmask |= 1 << ((ip->insn_opcode >> MIPS16OP_SH_MOVE32Z)
			      & MIPS16OP_MASK_MOVE32Z);
      if (pinfo & MIPS16_INSN_READ_GPR_X)
	mips_gprmask |= 1 << ((ip->insn_opcode >> MIPS16OP_SH_REGR32)
			      & MIPS16OP_MASK_REGR32);
    }

  if (mips_relax.sequence != 2 && !mips_opts.noreorder)
    {
      /* Filling the branch delay slot is more complex.  We try to
	 switch the branch with the previous instruction, which we can
	 do if the previous instruction does not set up a condition
	 that the branch tests and if the branch is not itself the
	 target of any branch.  */
      if ((pinfo & INSN_UNCOND_BRANCH_DELAY)
	  || (pinfo & INSN_COND_BRANCH_DELAY))
	{
	  if (mips_optimize < 2
	      /* If we have seen .set volatile or .set nomove, don't
		 optimize.  */
	      || mips_opts.nomove != 0
	      /* If we had to emit any NOP instructions, then we
		 already know we can not swap.  */
	      || nops != 0
	      /* If we don't even know the previous insn, we can not
		 swap.  */
	      || ! prev_insn_valid
	      /* If the previous insn is already in a branch delay
		 slot, then we can not swap.  */
	      || prev_insn_is_delay_slot
	      /* If the previous previous insn was in a .set
		 noreorder, we can't swap.  Actually, the MIPS
		 assembler will swap in this situation.  However, gcc
		 configured -with-gnu-as will generate code like
		   .set noreorder
		   lw	$4,XXX
		   .set	reorder
		   INSN
		   bne	$4,$0,foo
		 in which we can not swap the bne and INSN.  If gcc is
		 not configured -with-gnu-as, it does not output the
		 .set pseudo-ops.  We don't have to check
		 prev_insn_unreordered, because prev_insn_valid will
		 be 0 in that case.  We don't want to use
		 prev_prev_insn_valid, because we do want to be able
		 to swap at the start of a function.  */
	      || prev_prev_insn_unreordered
	      /* If the branch is itself the target of a branch, we
		 can not swap.  We cheat on this; all we check for is
		 whether there is a label on this instruction.  If
		 there are any branches to anything other than a
		 label, users must use .set noreorder.  */
	      || insn_labels != NULL
	      /* If the previous instruction is in a variant frag
		 other than this branch's one, we cannot do the swap.
		 This does not apply to the mips16, which uses variant
		 frags for different purposes.  */
	      || (! mips_opts.mips16
		  && prev_insn_frag_type == rs_machine_dependent)
	      /* If the branch reads the condition codes, we don't
		 even try to swap, because in the sequence
		   ctc1 $X,$31
		   INSN
		   INSN
		   bc1t LABEL
		 we can not swap, and I don't feel like handling that
		 case.  */
	      || (! mips_opts.mips16
		  && (pinfo & INSN_READ_COND_CODE)
		  && ! cop_interlocks)
	      /* We can not swap with an instruction that requires a
		 delay slot, because the target of the branch might
		 interfere with that instruction.  */
	      || (! mips_opts.mips16
		  && (prev_pinfo
              /* Itbl support may require additional care here.  */
		      & (INSN_LOAD_COPROC_DELAY
			 | INSN_COPROC_MOVE_DELAY
			 | INSN_WRITE_COND_CODE))
		  && ! cop_interlocks)
	      || (! (hilo_interlocks
		     || (mips_opts.arch == CPU_R3900 && (pinfo & INSN_MULT)))
		  && (prev_pinfo
		      & (INSN_READ_LO
			 | INSN_READ_HI)))
	      || (! mips_opts.mips16
		  && (prev_pinfo & INSN_LOAD_MEMORY_DELAY)
		  && ! gpr_interlocks)
	      || (! mips_opts.mips16
                  /* Itbl support may require additional care here.  */
		  && (prev_pinfo & INSN_COPROC_MEMORY_DELAY)
		  && ! cop_mem_interlocks)
	      /* We can not swap with a branch instruction.  */
	      || (prev_pinfo
		  & (INSN_UNCOND_BRANCH_DELAY
		     | INSN_COND_BRANCH_DELAY
		     | INSN_COND_BRANCH_LIKELY))
	      /* We do not swap with a trap instruction, since it
		 complicates trap handlers to have the trap
		 instruction be in a delay slot.  */
	      || (prev_pinfo & INSN_TRAP)
	      /* If the branch reads a register that the previous
		 instruction sets, we can not swap.  */
	      || (! mips_opts.mips16
		  && (prev_pinfo & INSN_WRITE_GPR_T)
		  && insn_uses_reg (ip,
				    ((prev_insn.insn_opcode >> OP_SH_RT)
				     & OP_MASK_RT),
				    MIPS_GR_REG))
	      || (! mips_opts.mips16
		  && (prev_pinfo & INSN_WRITE_GPR_D)
		  && insn_uses_reg (ip,
				    ((prev_insn.insn_opcode >> OP_SH_RD)
				     & OP_MASK_RD),
				    MIPS_GR_REG))
	      || (mips_opts.mips16
		  && (((prev_pinfo & MIPS16_INSN_WRITE_X)
		       && insn_uses_reg (ip,
					 ((prev_insn.insn_opcode
					   >> MIPS16OP_SH_RX)
					  & MIPS16OP_MASK_RX),
					 MIPS16_REG))
		      || ((prev_pinfo & MIPS16_INSN_WRITE_Y)
			  && insn_uses_reg (ip,
					    ((prev_insn.insn_opcode
					      >> MIPS16OP_SH_RY)
					     & MIPS16OP_MASK_RY),
					    MIPS16_REG))
		      || ((prev_pinfo & MIPS16_INSN_WRITE_Z)
			  && insn_uses_reg (ip,
					    ((prev_insn.insn_opcode
					      >> MIPS16OP_SH_RZ)
					     & MIPS16OP_MASK_RZ),
					    MIPS16_REG))
		      || ((prev_pinfo & MIPS16_INSN_WRITE_T)
			  && insn_uses_reg (ip, TREG, MIPS_GR_REG))
		      || ((prev_pinfo & MIPS16_INSN_WRITE_31)
			  && insn_uses_reg (ip, RA, MIPS_GR_REG))
		      || ((prev_pinfo & MIPS16_INSN_WRITE_GPR_Y)
			  && insn_uses_reg (ip,
					    MIPS16OP_EXTRACT_REG32R (prev_insn.
								     insn_opcode),
					    MIPS_GR_REG))))
	      /* If the branch writes a register that the previous
		 instruction sets, we can not swap (we know that
		 branches write only to RD or to $31).  */
	      || (! mips_opts.mips16
		  && (prev_pinfo & INSN_WRITE_GPR_T)
		  && (((pinfo & INSN_WRITE_GPR_D)
		       && (((prev_insn.insn_opcode >> OP_SH_RT) & OP_MASK_RT)
			   == ((ip->insn_opcode >> OP_SH_RD) & OP_MASK_RD)))
		      || ((pinfo & INSN_WRITE_GPR_31)
			  && (((prev_insn.insn_opcode >> OP_SH_RT)
			       & OP_MASK_RT)
			      == RA))))
	      || (! mips_opts.mips16
		  && (prev_pinfo & INSN_WRITE_GPR_D)
		  && (((pinfo & INSN_WRITE_GPR_D)
		       && (((prev_insn.insn_opcode >> OP_SH_RD) & OP_MASK_RD)
			   == ((ip->insn_opcode >> OP_SH_RD) & OP_MASK_RD)))
		      || ((pinfo & INSN_WRITE_GPR_31)
			  && (((prev_insn.insn_opcode >> OP_SH_RD)
			       & OP_MASK_RD)
			      == RA))))
	      || (mips_opts.mips16
		  && (pinfo & MIPS16_INSN_WRITE_31)
		  && ((prev_pinfo & MIPS16_INSN_WRITE_31)
		      || ((prev_pinfo & MIPS16_INSN_WRITE_GPR_Y)
			  && (MIPS16OP_EXTRACT_REG32R (prev_insn.insn_opcode)
			      == RA))))
	      /* If the branch writes a register that the previous
		 instruction reads, we can not swap (we know that
		 branches only write to RD or to $31).  */
	      || (! mips_opts.mips16
		  && (pinfo & INSN_WRITE_GPR_D)
		  && insn_uses_reg (&prev_insn,
				    ((ip->insn_opcode >> OP_SH_RD)
				     & OP_MASK_RD),
				    MIPS_GR_REG))
	      || (! mips_opts.mips16
		  && (pinfo & INSN_WRITE_GPR_31)
		  && insn_uses_reg (&prev_insn, RA, MIPS_GR_REG))
	      || (mips_opts.mips16
		  && (pinfo & MIPS16_INSN_WRITE_31)
		  && insn_uses_reg (&prev_insn, RA, MIPS_GR_REG))
	      /* If we are generating embedded PIC code, the branch
		 might be expanded into a sequence which uses $at, so
		 we can't swap with an instruction which reads it.  */
	      || (mips_pic == EMBEDDED_PIC
		  && insn_uses_reg (&prev_insn, AT, MIPS_GR_REG))
	      /* If the previous previous instruction has a load
		 delay, and sets a register that the branch reads, we
		 can not swap.  */
	      || (! mips_opts.mips16
              /* Itbl support may require additional care here.  */
		  && (((prev_prev_insn.insn_mo->pinfo & INSN_LOAD_COPROC_DELAY)
		       && ! cop_interlocks)
		      || ((prev_prev_insn.insn_mo->pinfo
			   & INSN_LOAD_MEMORY_DELAY)
			  && ! gpr_interlocks))
		  && insn_uses_reg (ip,
				    ((prev_prev_insn.insn_opcode >> OP_SH_RT)
				     & OP_MASK_RT),
				    MIPS_GR_REG))
	      /* If one instruction sets a condition code and the
                 other one uses a condition code, we can not swap.  */
	      || ((pinfo & INSN_READ_COND_CODE)
		  && (prev_pinfo & INSN_WRITE_COND_CODE))
	      || ((pinfo & INSN_WRITE_COND_CODE)
		  && (prev_pinfo & INSN_READ_COND_CODE))
	      /* If the previous instruction uses the PC, we can not
                 swap.  */
	      || (mips_opts.mips16
		  && (prev_pinfo & MIPS16_INSN_READ_PC))
	      /* If the previous instruction was extended, we can not
                 swap.  */
	      || (mips_opts.mips16 && prev_insn_extended)
	      /* If the previous instruction had a fixup in mips16
                 mode, we can not swap.  This normally means that the
                 previous instruction was a 4 byte branch anyhow.  */
	      || (mips_opts.mips16 && prev_insn_fixp[0])
	      /* If the previous instruction is a sync, sync.l, or
		 sync.p, we can not swap.  */
	      || (prev_pinfo & INSN_SYNC))
	    {
	      /* We could do even better for unconditional branches to
		 portions of this object file; we could pick up the
		 instruction at the destination, put it in the delay
		 slot, and bump the destination address.  */
	      emit_nop ();
	      /* Update the previous insn information.  */
	      prev_prev_insn = *ip;
	      prev_insn.insn_mo = &dummy_opcode;
	    }
	  else
	    {
	      /* It looks like we can actually do the swap.  */
	      if (! mips_opts.mips16)
		{
		  char *prev_f;
		  char temp[4];

		  prev_f = prev_insn_frag->fr_literal + prev_insn_where;
		  if (!relaxed_branch)
		    {
		      /* If this is not a relaxed branch, then just
			 swap the instructions.  */
		      memcpy (temp, prev_f, 4);
		      memcpy (prev_f, f, 4);
		      memcpy (f, temp, 4);
		    }
		  else
		    {
		      /* If this is a relaxed branch, then we move the
			 instruction to be placed in the delay slot to
			 the current frag, shrinking the fixed part of
			 the originating frag.  If the branch occupies
			 the tail of the latter, we move it backwards,
			 into the space freed by the moved instruction.  */
		      f = frag_more (4);
		      memcpy (f, prev_f, 4);
		      prev_insn_frag->fr_fix -= 4;
		      if (prev_insn_frag->fr_type == rs_machine_dependent)
			memmove (prev_f, prev_f + 4, prev_insn_frag->fr_var);
		    }

		  if (prev_insn_fixp[0])
		    {
		      prev_insn_fixp[0]->fx_frag = frag_now;
		      prev_insn_fixp[0]->fx_where = f - frag_now->fr_literal;
		    }
		  if (prev_insn_fixp[1])
		    {
		      prev_insn_fixp[1]->fx_frag = frag_now;
		      prev_insn_fixp[1]->fx_where = f - frag_now->fr_literal;
		    }
		  if (prev_insn_fixp[2])
		    {
		      prev_insn_fixp[2]->fx_frag = frag_now;
		      prev_insn_fixp[2]->fx_where = f - frag_now->fr_literal;
		    }
		  if (prev_insn_fixp[0] && HAVE_NEWABI
		      && prev_insn_frag != frag_now
		      && (prev_insn_fixp[0]->fx_r_type
			  == BFD_RELOC_MIPS_GOT_DISP
			  || (prev_insn_fixp[0]->fx_r_type
			      == BFD_RELOC_MIPS_CALL16)))
		    {
		      /* To avoid confusion in tc_gen_reloc, we must
			 ensure that this does not become a variant
			 frag.  */
		      force_new_frag = TRUE;
		    }

		  if (!relaxed_branch)
		    {
		      if (fixp[0])
			{
			  fixp[0]->fx_frag = prev_insn_frag;
			  fixp[0]->fx_where = prev_insn_where;
			}
		      if (fixp[1])
			{
			  fixp[1]->fx_frag = prev_insn_frag;
			  fixp[1]->fx_where = prev_insn_where;
			}
		      if (fixp[2])
			{
			  fixp[2]->fx_frag = prev_insn_frag;
			  fixp[2]->fx_where = prev_insn_where;
			}
		    }
		  else if (prev_insn_frag->fr_type == rs_machine_dependent)
		    {
		      if (fixp[0])
			fixp[0]->fx_where -= 4;
		      if (fixp[1])
			fixp[1]->fx_where -= 4;
		      if (fixp[2])
			fixp[2]->fx_where -= 4;
		    }
		}
	      else
		{
		  char *prev_f;
		  char temp[2];

		  assert (prev_insn_fixp[0] == NULL);
		  assert (prev_insn_fixp[1] == NULL);
		  assert (prev_insn_fixp[2] == NULL);
		  prev_f = prev_insn_frag->fr_literal + prev_insn_where;
		  memcpy (temp, prev_f, 2);
		  memcpy (prev_f, f, 2);
		  if (*reloc_type != BFD_RELOC_MIPS16_JMP)
		    {
		      assert (*reloc_type == BFD_RELOC_UNUSED);
		      memcpy (f, temp, 2);
		    }
		  else
		    {
		      memcpy (f, f + 2, 2);
		      memcpy (f + 2, temp, 2);
		    }
		  if (fixp[0])
		    {
		      fixp[0]->fx_frag = prev_insn_frag;
		      fixp[0]->fx_where = prev_insn_where;
		    }
		  if (fixp[1])
		    {
		      fixp[1]->fx_frag = prev_insn_frag;
		      fixp[1]->fx_where = prev_insn_where;
		    }
		  if (fixp[2])
		    {
		      fixp[2]->fx_frag = prev_insn_frag;
		      fixp[2]->fx_where = prev_insn_where;
		    }
		}

	      /* Update the previous insn information; leave prev_insn
		 unchanged.  */
	      prev_prev_insn = *ip;
	    }
	  prev_insn_is_delay_slot = 1;

	  /* If that was an unconditional branch, forget the previous
	     insn information.  */
	  if (pinfo & INSN_UNCOND_BRANCH_DELAY)
	    {
	      prev_prev_insn.insn_mo = &dummy_opcode;
	      prev_insn.insn_mo = &dummy_opcode;
	    }

	  prev_insn_fixp[0] = NULL;
	  prev_insn_fixp[1] = NULL;
	  prev_insn_fixp[2] = NULL;
	  prev_insn_reloc_type[0] = BFD_RELOC_UNUSED;
	  prev_insn_reloc_type[1] = BFD_RELOC_UNUSED;
	  prev_insn_reloc_type[2] = BFD_RELOC_UNUSED;
	  prev_insn_extended = 0;
	}
      else if (pinfo & INSN_COND_BRANCH_LIKELY)
	{
	  /* We don't yet optimize a branch likely.  What we should do
	     is look at the target, copy the instruction found there
	     into the delay slot, and increment the branch to jump to
	     the next instruction.  */
	  emit_nop ();
	  /* Update the previous insn information.  */
	  prev_prev_insn = *ip;
	  prev_insn.insn_mo = &dummy_opcode;
	  prev_insn_fixp[0] = NULL;
	  prev_insn_fixp[1] = NULL;
	  prev_insn_fixp[2] = NULL;
	  prev_insn_reloc_type[0] = BFD_RELOC_UNUSED;
	  prev_insn_reloc_type[1] = BFD_RELOC_UNUSED;
	  prev_insn_reloc_type[2] = BFD_RELOC_UNUSED;
	  prev_insn_extended = 0;
	}
      else
	{
	  /* Update the previous insn information.  */
	  if (nops > 0)
	    prev_prev_insn.insn_mo = &dummy_opcode;
	  else
	    prev_prev_insn = prev_insn;
	  prev_insn = *ip;

	  /* Any time we see a branch, we always fill the delay slot
	     immediately; since this insn is not a branch, we know it
	     is not in a delay slot.  */
	  prev_insn_is_delay_slot = 0;

	  prev_insn_fixp[0] = fixp[0];
	  prev_insn_fixp[1] = fixp[1];
	  prev_insn_fixp[2] = fixp[2];
	  prev_insn_reloc_type[0] = reloc_type[0];
	  prev_insn_reloc_type[1] = reloc_type[1];
	  prev_insn_reloc_type[2] = reloc_type[2];
	  if (mips_opts.mips16)
	    prev_insn_extended = (ip->use_extend
				  || *reloc_type > BFD_RELOC_UNUSED);
	}

      prev_prev_insn_unreordered = prev_insn_unreordered;
      prev_insn_unreordered = 0;
      prev_insn_frag = frag_now;
      prev_insn_where = f - frag_now->fr_literal;
      prev_insn_valid = 1;
    }
  else if (mips_relax.sequence != 2)
    {
      /* We need to record a bit of information even when we are not
         reordering, in order to determine the base address for mips16
         PC relative relocs.  */
      prev_prev_insn = prev_insn;
      prev_insn = *ip;
      prev_insn_reloc_type[0] = reloc_type[0];
      prev_insn_reloc_type[1] = reloc_type[1];
      prev_insn_reloc_type[2] = reloc_type[2];
      prev_prev_insn_unreordered = prev_insn_unreordered;
      prev_insn_unreordered = 1;
    }

  /* We just output an insn, so the next one doesn't have a label.  */
  mips_clear_insn_labels ();
}

/* This function forgets that there was any previous instruction or
   label.  If PRESERVE is non-zero, it remembers enough information to
   know whether nops are needed before a noreorder section.  */

static void
mips_no_prev_insn (int preserve)
{
  if (! preserve)
    {
      prev_insn.insn_mo = &dummy_opcode;
      prev_prev_insn.insn_mo = &dummy_opcode;
      prev_nop_frag = NULL;
      prev_nop_frag_holds = 0;
      prev_nop_frag_required = 0;
      prev_nop_frag_since = 0;
    }
  prev_insn_valid = 0;
  prev_insn_is_delay_slot = 0;
  prev_insn_unreordered = 0;
  prev_insn_extended = 0;
  prev_insn_reloc_type[0] = BFD_RELOC_UNUSED;
  prev_insn_reloc_type[1] = BFD_RELOC_UNUSED;
  prev_insn_reloc_type[2] = BFD_RELOC_UNUSED;
  prev_prev_insn_unreordered = 0;
  mips_clear_insn_labels ();
}

/* This function must be called whenever we turn on noreorder or emit
   something other than instructions.  It inserts any NOPS which might
   be needed by the previous instruction, and clears the information
   kept for the previous instructions.  The INSNS parameter is true if
   instructions are to follow.  */

static void
mips_emit_delays (bfd_boolean insns)
{
  if (! mips_opts.noreorder)
    {
      int nops;

      nops = 0;
      if ((! mips_opts.mips16
	   && ((prev_insn.insn_mo->pinfo
		& (INSN_LOAD_COPROC_DELAY
		   | INSN_COPROC_MOVE_DELAY
		   | INSN_WRITE_COND_CODE))
	       && ! cop_interlocks))
	  || (! hilo_interlocks
	      && (prev_insn.insn_mo->pinfo
		  & (INSN_READ_LO
		     | INSN_READ_HI)))
	  || (! mips_opts.mips16
	      && (prev_insn.insn_mo->pinfo & INSN_LOAD_MEMORY_DELAY)
	      && ! gpr_interlocks)
	  || (! mips_opts.mips16
	      && (prev_insn.insn_mo->pinfo & INSN_COPROC_MEMORY_DELAY)
	      && ! cop_mem_interlocks))
	{
	  /* Itbl support may require additional care here.  */
	  ++nops;
	  if ((! mips_opts.mips16
	       && ((prev_insn.insn_mo->pinfo & INSN_WRITE_COND_CODE)
		   && ! cop_interlocks))
	      || (! hilo_interlocks
		  && ((prev_insn.insn_mo->pinfo & INSN_READ_HI)
		      || (prev_insn.insn_mo->pinfo & INSN_READ_LO))))
	    ++nops;

	  if (prev_insn_unreordered)
	    nops = 0;
	}
      else if ((! mips_opts.mips16
		&& ((prev_prev_insn.insn_mo->pinfo & INSN_WRITE_COND_CODE)
		    && ! cop_interlocks))
	       || (! hilo_interlocks
		   && ((prev_prev_insn.insn_mo->pinfo & INSN_READ_HI)
		       || (prev_prev_insn.insn_mo->pinfo & INSN_READ_LO))))
	{
	  /* Itbl support may require additional care here.  */
	  if (! prev_prev_insn_unreordered)
	    ++nops;
	}

      if (mips_fix_4122_bugs && prev_insn.insn_mo->name)
	{
	  int min_nops = 0;
	  const char *pn = prev_insn.insn_mo->name;
	  if (strncmp(pn, "macc", 4) == 0
	      || strncmp(pn, "dmacc", 5) == 0
	      || strncmp(pn, "dmult", 5) == 0)
	    {
	      min_nops = 1;
	    }
	  if (nops < min_nops)
	    nops = min_nops;
	}

      if (nops > 0)
	{
	  struct insn_label_list *l;

	  if (insns)
	    {
	      /* Record the frag which holds the nop instructions, so
                 that we can remove them if we don't need them.  */
	      frag_grow (mips_opts.mips16 ? nops * 2 : nops * 4);
	      prev_nop_frag = frag_now;
	      prev_nop_frag_holds = nops;
	      prev_nop_frag_required = 0;
	      prev_nop_frag_since = 0;
	    }

	  for (; nops > 0; --nops)
	    emit_nop ();

	  if (insns)
	    {
	      /* Move on to a new frag, so that it is safe to simply
                 decrease the size of prev_nop_frag.  */
	      frag_wane (frag_now);
	      frag_new (0);
	    }

	  for (l = insn_labels; l != NULL; l = l->next)
	    {
	      valueT val;

	      assert (S_GET_SEGMENT (l->label) == now_seg);
	      symbol_set_frag (l->label, frag_now);
	      val = (valueT) frag_now_fix ();
	      /* mips16 text labels are stored as odd.  */
	      if (mips_opts.mips16)
		++val;
	      S_SET_VALUE (l->label, val);
	    }
	}
    }

  /* Mark instruction labels in mips16 mode.  */
  if (insns)
    mips16_mark_labels ();

  mips_no_prev_insn (insns);
}

/* Set up global variables for the start of a new macro.  */

static void
macro_start (void)
{
  memset (&mips_macro_warning.sizes, 0, sizeof (mips_macro_warning.sizes));
  mips_macro_warning.delay_slot_p = (mips_opts.noreorder
				     && (prev_insn.insn_mo->pinfo
					 & (INSN_UNCOND_BRANCH_DELAY
					    | INSN_COND_BRANCH_DELAY
					    | INSN_COND_BRANCH_LIKELY)) != 0);
}

/* Given that a macro is longer than 4 bytes, return the appropriate warning
   for it.  Return null if no warning is needed.  SUBTYPE is a bitmask of
   RELAX_DELAY_SLOT and RELAX_NOMACRO.  */

static const char *
macro_warning (relax_substateT subtype)
{
  if (subtype & RELAX_DELAY_SLOT)
    return _("Macro instruction expanded into multiple instructions"
	     " in a branch delay slot");
  else if (subtype & RELAX_NOMACRO)
    return _("Macro instruction expanded into multiple instructions");
  else
    return 0;
}

/* Finish up a macro.  Emit warnings as appropriate.  */

static void
macro_end (void)
{
  if (mips_macro_warning.sizes[0] > 4 || mips_macro_warning.sizes[1] > 4)
    {
      relax_substateT subtype;

      /* Set up the relaxation warning flags.  */
      subtype = 0;
      if (mips_macro_warning.sizes[1] > mips_macro_warning.sizes[0])
	subtype |= RELAX_SECOND_LONGER;
      if (mips_opts.warn_about_macros)
	subtype |= RELAX_NOMACRO;
      if (mips_macro_warning.delay_slot_p)
	subtype |= RELAX_DELAY_SLOT;

      if (mips_macro_warning.sizes[0] > 4 && mips_macro_warning.sizes[1] > 4)
	{
	  /* Either the macro has a single implementation or both
	     implementations are longer than 4 bytes.  Emit the
	     warning now.  */
	  const char *msg = macro_warning (subtype);
	  if (msg != 0)
	    as_warn (msg);
	}
      else
	{
	  /* One implementation might need a warning but the other
	     definitely doesn't.  */
	  mips_macro_warning.first_frag->fr_subtype |= subtype;
	}
    }
}

/* Build an instruction created by a macro expansion.  This is passed
   a pointer to the count of instructions created so far, an
   expression, the name of the instruction to build, an operand format
   string, and corresponding arguments.  */

static void
macro_build (expressionS *ep, const char *name, const char *fmt, ...)
{
  struct mips_cl_insn insn;
  bfd_reloc_code_real_type r[3];
  va_list args;

  va_start (args, fmt);

  if (mips_opts.mips16)
    {
      mips16_macro_build (ep, name, fmt, args);
      va_end (args);
      return;
    }

  r[0] = BFD_RELOC_UNUSED;
  r[1] = BFD_RELOC_UNUSED;
  r[2] = BFD_RELOC_UNUSED;
  insn.insn_mo = (struct mips_opcode *) hash_find (op_hash, name);
  assert (insn.insn_mo);
  assert (strcmp (name, insn.insn_mo->name) == 0);

  /* Search until we get a match for NAME.  */
  while (1)
    {
      /* It is assumed here that macros will never generate
         MDMX or MIPS-3D instructions.  */
      if (strcmp (fmt, insn.insn_mo->args) == 0
	  && insn.insn_mo->pinfo != INSN_MACRO
  	  && OPCODE_IS_MEMBER (insn.insn_mo,
  			       (mips_opts.isa
	      		        | (file_ase_mips16 ? INSN_MIPS16 : 0)),
			       mips_opts.arch)
	  && (mips_opts.arch != CPU_R4650 || (insn.insn_mo->pinfo & FP_D) == 0))
	break;

      ++insn.insn_mo;
      assert (insn.insn_mo->name);
      assert (strcmp (name, insn.insn_mo->name) == 0);
    }

  insn.insn_opcode = insn.insn_mo->match;
  for (;;)
    {
      switch (*fmt++)
	{
	case '\0':
	  break;

	case ',':
	case '(':
	case ')':
	  continue;

	case '+':
	  switch (*fmt++)
	    {
	    case 'A':
	    case 'E':
	      insn.insn_opcode |= (va_arg (args, int)
				   & OP_MASK_SHAMT) << OP_SH_SHAMT;
	      continue;

	    case 'B':
	    case 'F':
	      /* Note that in the macro case, these arguments are already
		 in MSB form.  (When handling the instruction in the
		 non-macro case, these arguments are sizes from which
		 MSB values must be calculated.)  */
	      insn.insn_opcode |= (va_arg (args, int)
				   & OP_MASK_INSMSB) << OP_SH_INSMSB;
	      continue;

	    case 'C':
	    case 'G':
	    case 'H':
	      /* Note that in the macro case, these arguments are already
		 in MSBD form.  (When handling the instruction in the
		 non-macro case, these arguments are sizes from which
		 MSBD values must be calculated.)  */
	      insn.insn_opcode |= (va_arg (args, int)
				   & OP_MASK_EXTMSBD) << OP_SH_EXTMSBD;
	      continue;

	    default:
	      internalError ();
	    }
	  continue;

	case 't':
	case 'w':
	case 'E':
	  insn.insn_opcode |= va_arg (args, int) << OP_SH_RT;
	  continue;

	case 'c':
	  insn.insn_opcode |= va_arg (args, int) << OP_SH_CODE;
	  continue;

	case 'T':
	case 'W':
	  insn.insn_opcode |= va_arg (args, int) << OP_SH_FT;
	  continue;

	case 'd':
	case 'G':
	case 'K':
	  insn.insn_opcode |= va_arg (args, int) << OP_SH_RD;
	  continue;

	case 'U':
	  {
	    int tmp = va_arg (args, int);

	    insn.insn_opcode |= tmp << OP_SH_RT;
	    insn.insn_opcode |= tmp << OP_SH_RD;
	    continue;
	  }

	case 'V':
	case 'S':
	  insn.insn_opcode |= va_arg (args, int) << OP_SH_FS;
	  continue;

	case 'z':
	  continue;

	case '<':
	  insn.insn_opcode |= va_arg (args, int) << OP_SH_SHAMT;
	  continue;

	case 'D':
	  insn.insn_opcode |= va_arg (args, int) << OP_SH_FD;
	  continue;

	case 'B':
	  insn.insn_opcode |= va_arg (args, int) << OP_SH_CODE20;
	  continue;

	case 'J':
	  insn.insn_opcode |= va_arg (args, int) << OP_SH_CODE19;
	  continue;

	case 'q':
	  insn.insn_opcode |= va_arg (args, int) << OP_SH_CODE2;
	  continue;

	case 'b':
	case 's':
	case 'r':
	case 'v':
	  insn.insn_opcode |= va_arg (args, int) << OP_SH_RS;
	  continue;

	case 'i':
	case 'j':
	case 'o':
	  *r = (bfd_reloc_code_real_type) va_arg (args, int);
	  assert (*r == BFD_RELOC_GPREL16
		  || *r == BFD_RELOC_MIPS_LITERAL
		  || *r == BFD_RELOC_MIPS_HIGHER
		  || *r == BFD_RELOC_HI16_S
		  || *r == BFD_RELOC_LO16
		  || *r == BFD_RELOC_MIPS_GOT16
		  || *r == BFD_RELOC_MIPS_CALL16
		  || *r == BFD_RELOC_MIPS_GOT_DISP
		  || *r == BFD_RELOC_MIPS_GOT_PAGE
		  || *r == BFD_RELOC_MIPS_GOT_OFST
		  || *r == BFD_RELOC_MIPS_GOT_LO16
		  || *r == BFD_RELOC_MIPS_CALL_LO16
		  || (ep->X_op == O_subtract
		      && *r == BFD_RELOC_PCREL_LO16));
	  continue;

	case 'u':
	  *r = (bfd_reloc_code_real_type) va_arg (args, int);
	  assert (ep != NULL
		  && (ep->X_op == O_constant
		      || (ep->X_op == O_symbol
			  && (*r == BFD_RELOC_MIPS_HIGHEST
			      || *r == BFD_RELOC_HI16_S
			      || *r == BFD_RELOC_HI16
			      || *r == BFD_RELOC_GPREL16
			      || *r == BFD_RELOC_MIPS_GOT_HI16
			      || *r == BFD_RELOC_MIPS_CALL_HI16))
		      || (ep->X_op == O_subtract
			  && *r == BFD_RELOC_PCREL_HI16_S)));
	  continue;

	case 'p':
	  assert (ep != NULL);
	  /*
	   * This allows macro() to pass an immediate expression for
	   * creating short branches without creating a symbol.
	   * Note that the expression still might come from the assembly
	   * input, in which case the value is not checked for range nor
	   * is a relocation entry generated (yuck).
	   */
	  if (ep->X_op == O_constant)
	    {
	      insn.insn_opcode |= (ep->X_add_number >> 2) & 0xffff;
	      ep = NULL;
	    }
	  else
	    *r = BFD_RELOC_16_PCREL_S2;
	  continue;

	case 'a':
	  assert (ep != NULL);
	  *r = BFD_RELOC_MIPS_JMP;
	  continue;

	case 'C':
	  insn.insn_opcode |= va_arg (args, unsigned long);
	  continue;

	default:
	  internalError ();
	}
      break;
    }
  va_end (args);
  assert (*r == BFD_RELOC_UNUSED ? ep == NULL : ep != NULL);

  append_insn (&insn, ep, r);
}

static void
mips16_macro_build (expressionS *ep, const char *name, const char *fmt,
		    va_list args)
{
  struct mips_cl_insn insn;
  bfd_reloc_code_real_type r[3]
    = {BFD_RELOC_UNUSED, BFD_RELOC_UNUSED, BFD_RELOC_UNUSED};

  insn.insn_mo = (struct mips_opcode *) hash_find (mips16_op_hash, name);
  assert (insn.insn_mo);
  assert (strcmp (name, insn.insn_mo->name) == 0);

  while (strcmp (fmt, insn.insn_mo->args) != 0
	 || insn.insn_mo->pinfo == INSN_MACRO)
    {
      ++insn.insn_mo;
      assert (insn.insn_mo->name);
      assert (strcmp (name, insn.insn_mo->name) == 0);
    }

  insn.insn_opcode = insn.insn_mo->match;
  insn.use_extend = FALSE;

  for (;;)
    {
      int c;

      c = *fmt++;
      switch (c)
	{
	case '\0':
	  break;

	case ',':
	case '(':
	case ')':
	  continue;

	case 'y':
	case 'w':
	  insn.insn_opcode |= va_arg (args, int) << MIPS16OP_SH_RY;
	  continue;

	case 'x':
	case 'v':
	  insn.insn_opcode |= va_arg (args, int) << MIPS16OP_SH_RX;
	  continue;

	case 'z':
	  insn.insn_opcode |= va_arg (args, int) << MIPS16OP_SH_RZ;
	  continue;

	case 'Z':
	  insn.insn_opcode |= va_arg (args, int) << MIPS16OP_SH_MOVE32Z;
	  continue;

	case '0':
	case 'S':
	case 'P':
	case 'R':
	  continue;

	case 'X':
	  insn.insn_opcode |= va_arg (args, int) << MIPS16OP_SH_REGR32;
	  continue;

	case 'Y':
	  {
	    int regno;

	    regno = va_arg (args, int);
	    regno = ((regno & 7) << 2) | ((regno & 0x18) >> 3);
	    insn.insn_opcode |= regno << MIPS16OP_SH_REG32R;
	  }
	  continue;

	case '<':
	case '>':
	case '4':
	case '5':
	case 'H':
	case 'W':
	case 'D':
	case 'j':
	case '8':
	case 'V':
	case 'C':
	case 'U':
	case 'k':
	case 'K':
	case 'p':
	case 'q':
	  {
	    assert (ep != NULL);

	    if (ep->X_op != O_constant)
	      *r = (int) BFD_RELOC_UNUSED + c;
	    else
	      {
		mips16_immed (NULL, 0, c, ep->X_add_number, FALSE, FALSE,
			      FALSE, &insn.insn_opcode, &insn.use_extend,
			      &insn.extend);
		ep = NULL;
		*r = BFD_RELOC_UNUSED;
	      }
	  }
	  continue;

	case '6':
	  insn.insn_opcode |= va_arg (args, int) << MIPS16OP_SH_IMM6;
	  continue;
	}

      break;
    }

  assert (*r == BFD_RELOC_UNUSED ? ep == NULL : ep != NULL);

  append_insn (&insn, ep, r);
}

/*
 * Generate a "jalr" instruction with a relocation hint to the called
 * function.  This occurs in NewABI PIC code.
 */
static void
macro_build_jalr (expressionS *ep)
{
  char *f = NULL;

  if (HAVE_NEWABI)
    {
      frag_grow (8);
      f = frag_more (0);
    }
  macro_build (NULL, "jalr", "d,s", RA, PIC_CALL_REG);
  if (HAVE_NEWABI)
    fix_new_exp (frag_now, f - frag_now->fr_literal,
		 4, ep, FALSE, BFD_RELOC_MIPS_JALR);
}

/*
 * Generate a "lui" instruction.
 */
static void
macro_build_lui (expressionS *ep, int regnum)
{
  expressionS high_expr;
  struct mips_cl_insn insn;
  bfd_reloc_code_real_type r[3]
    = {BFD_RELOC_UNUSED, BFD_RELOC_UNUSED, BFD_RELOC_UNUSED};
  const char *name = "lui";
  const char *fmt = "t,u";

  assert (! mips_opts.mips16);

  high_expr = *ep;

  if (high_expr.X_op == O_constant)
    {
      /* we can compute the instruction now without a relocation entry */
      high_expr.X_add_number = ((high_expr.X_add_number + 0x8000)
				>> 16) & 0xffff;
      *r = BFD_RELOC_UNUSED;
    }
  else
    {
      assert (ep->X_op == O_symbol);
      /* _gp_disp is a special case, used from s_cpload.  */
      assert (mips_pic == NO_PIC
	      || (! HAVE_NEWABI
		  && strcmp (S_GET_NAME (ep->X_add_symbol), "_gp_disp") == 0));
      *r = BFD_RELOC_HI16_S;
    }

  insn.insn_mo = (struct mips_opcode *) hash_find (op_hash, name);
  assert (insn.insn_mo);
  assert (strcmp (name, insn.insn_mo->name) == 0);
  assert (strcmp (fmt, insn.insn_mo->args) == 0);

  insn.insn_opcode = insn.insn_mo->match | (regnum << OP_SH_RT);
  if (*r == BFD_RELOC_UNUSED)
    {
      insn.insn_opcode |= high_expr.X_add_number;
      append_insn (&insn, NULL, r);
    }
  else
    append_insn (&insn, &high_expr, r);
}

/* Generate a sequence of instructions to do a load or store from a constant
   offset off of a base register (breg) into/from a target register (treg),
   using AT if necessary.  */
static void
macro_build_ldst_constoffset (expressionS *ep, const char *op,
			      int treg, int breg, int dbl)
{
  assert (ep->X_op == O_constant);

  /* Sign-extending 32-bit constants makes their handling easier.  */
  if (! dbl && ! ((ep->X_add_number & ~((bfd_vma) 0x7fffffff))
		  == ~((bfd_vma) 0x7fffffff)))
    {
      if (ep->X_add_number & ~((bfd_vma) 0xffffffff))
	as_bad (_("constant too large"));

      ep->X_add_number = (((ep->X_add_number & 0xffffffff) ^ 0x80000000)
			  - 0x80000000);
    }

  /* Right now, this routine can only handle signed 32-bit constants.  */
  if (! IS_SEXT_32BIT_NUM(ep->X_add_number + 0x8000))
    as_warn (_("operand overflow"));

  if (IS_SEXT_16BIT_NUM(ep->X_add_number))
    {
      /* Signed 16-bit offset will fit in the op.  Easy!  */
      macro_build (ep, op, "t,o(b)", treg, BFD_RELOC_LO16, breg);
    }
  else
    {
      /* 32-bit offset, need multiple instructions and AT, like:
	   lui      $tempreg,const_hi       (BFD_RELOC_HI16_S)
	   addu     $tempreg,$tempreg,$breg
           <op>     $treg,const_lo($tempreg)   (BFD_RELOC_LO16)
         to handle the complete offset.  */
      macro_build_lui (ep, AT);
      macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", AT, AT, breg);
      macro_build (ep, op, "t,o(b)", treg, BFD_RELOC_LO16, AT);

      if (mips_opts.noat)
	as_warn (_("Macro used $at after \".set noat\""));
    }
}

/*			set_at()
 * Generates code to set the $at register to true (one)
 * if reg is less than the immediate expression.
 */
static void
set_at (int reg, int unsignedp)
{
  if (imm_expr.X_op == O_constant
      && imm_expr.X_add_number >= -0x8000
      && imm_expr.X_add_number < 0x8000)
    macro_build (&imm_expr, unsignedp ? "sltiu" : "slti", "t,r,j",
		 AT, reg, BFD_RELOC_LO16);
  else
    {
      load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
      macro_build (NULL, unsignedp ? "sltu" : "slt", "d,v,t", AT, reg, AT);
    }
}

static void
normalize_constant_expr (expressionS *ex)
{
  if (ex->X_op == O_constant && HAVE_32BIT_GPRS)
    ex->X_add_number = (((ex->X_add_number & 0xffffffff) ^ 0x80000000)
			- 0x80000000);
}

/* Warn if an expression is not a constant.  */

static void
check_absolute_expr (struct mips_cl_insn *ip, expressionS *ex)
{
  if (ex->X_op == O_big)
    as_bad (_("unsupported large constant"));
  else if (ex->X_op != O_constant)
    as_bad (_("Instruction %s requires absolute expression"), ip->insn_mo->name);

  normalize_constant_expr (ex);
}

/* Count the leading zeroes by performing a binary chop. This is a
   bulky bit of source, but performance is a LOT better for the
   majority of values than a simple loop to count the bits:
       for (lcnt = 0; (lcnt < 32); lcnt++)
         if ((v) & (1 << (31 - lcnt)))
           break;
  However it is not code size friendly, and the gain will drop a bit
  on certain cached systems.
*/
#define COUNT_TOP_ZEROES(v)             \
  (((v) & ~0xffff) == 0                 \
   ? ((v) & ~0xff) == 0                 \
     ? ((v) & ~0xf) == 0                \
       ? ((v) & ~0x3) == 0              \
         ? ((v) & ~0x1) == 0            \
           ? !(v)                       \
             ? 32                       \
             : 31                       \
           : 30                         \
         : ((v) & ~0x7) == 0            \
           ? 29                         \
           : 28                         \
       : ((v) & ~0x3f) == 0             \
         ? ((v) & ~0x1f) == 0           \
           ? 27                         \
           : 26                         \
         : ((v) & ~0x7f) == 0           \
           ? 25                         \
           : 24                         \
     : ((v) & ~0xfff) == 0              \
       ? ((v) & ~0x3ff) == 0            \
         ? ((v) & ~0x1ff) == 0          \
           ? 23                         \
           : 22                         \
         : ((v) & ~0x7ff) == 0          \
           ? 21                         \
           : 20                         \
       : ((v) & ~0x3fff) == 0           \
         ? ((v) & ~0x1fff) == 0         \
           ? 19                         \
           : 18                         \
         : ((v) & ~0x7fff) == 0         \
           ? 17                         \
           : 16                         \
   : ((v) & ~0xffffff) == 0             \
     ? ((v) & ~0xfffff) == 0            \
       ? ((v) & ~0x3ffff) == 0          \
         ? ((v) & ~0x1ffff) == 0        \
           ? 15                         \
           : 14                         \
         : ((v) & ~0x7ffff) == 0        \
           ? 13                         \
           : 12                         \
       : ((v) & ~0x3fffff) == 0         \
         ? ((v) & ~0x1fffff) == 0       \
           ? 11                         \
           : 10                         \
         : ((v) & ~0x7fffff) == 0       \
           ? 9                          \
           : 8                          \
     : ((v) & ~0xfffffff) == 0          \
       ? ((v) & ~0x3ffffff) == 0        \
         ? ((v) & ~0x1ffffff) == 0      \
           ? 7                          \
           : 6                          \
         : ((v) & ~0x7ffffff) == 0      \
           ? 5                          \
           : 4                          \
       : ((v) & ~0x3fffffff) == 0       \
         ? ((v) & ~0x1fffffff) == 0     \
           ? 3                          \
           : 2                          \
         : ((v) & ~0x7fffffff) == 0     \
           ? 1                          \
           : 0)

/*			load_register()
 *  This routine generates the least number of instructions necessary to load
 *  an absolute expression value into a register.
 */
static void
load_register (int reg, expressionS *ep, int dbl)
{
  int freg;
  expressionS hi32, lo32;

  if (ep->X_op != O_big)
    {
      assert (ep->X_op == O_constant);

      /* Sign-extending 32-bit constants makes their handling easier.  */
      if (! dbl && ! ((ep->X_add_number & ~((bfd_vma) 0x7fffffff))
		      == ~((bfd_vma) 0x7fffffff)))
	{
	  if (ep->X_add_number & ~((bfd_vma) 0xffffffff))
	    as_bad (_("constant too large"));

	  ep->X_add_number = (((ep->X_add_number & 0xffffffff) ^ 0x80000000)
			      - 0x80000000);
	}

      if (IS_SEXT_16BIT_NUM (ep->X_add_number))
	{
	  /* We can handle 16 bit signed values with an addiu to
	     $zero.  No need to ever use daddiu here, since $zero and
	     the result are always correct in 32 bit mode.  */
	  macro_build (ep, "addiu", "t,r,j", reg, 0, BFD_RELOC_LO16);
	  return;
	}
      else if (ep->X_add_number >= 0 && ep->X_add_number < 0x10000)
	{
	  /* We can handle 16 bit unsigned values with an ori to
             $zero.  */
	  macro_build (ep, "ori", "t,r,i", reg, 0, BFD_RELOC_LO16);
	  return;
	}
      else if ((IS_SEXT_32BIT_NUM (ep->X_add_number)))
	{
	  /* 32 bit values require an lui.  */
	  macro_build (ep, "lui", "t,u", reg, BFD_RELOC_HI16);
	  if ((ep->X_add_number & 0xffff) != 0)
	    macro_build (ep, "ori", "t,r,i", reg, reg, BFD_RELOC_LO16);
	  return;
	}
    }

  /* The value is larger than 32 bits.  */

  if (HAVE_32BIT_GPRS)
    {
      as_bad (_("Number (0x%lx) larger than 32 bits"),
	      (unsigned long) ep->X_add_number);
      macro_build (ep, "addiu", "t,r,j", reg, 0, BFD_RELOC_LO16);
      return;
    }

  if (ep->X_op != O_big)
    {
      hi32 = *ep;
      hi32.X_add_number = (valueT) hi32.X_add_number >> 16;
      hi32.X_add_number = (valueT) hi32.X_add_number >> 16;
      hi32.X_add_number &= 0xffffffff;
      lo32 = *ep;
      lo32.X_add_number &= 0xffffffff;
    }
  else
    {
      assert (ep->X_add_number > 2);
      if (ep->X_add_number == 3)
	generic_bignum[3] = 0;
      else if (ep->X_add_number > 4)
	as_bad (_("Number larger than 64 bits"));
      lo32.X_op = O_constant;
      lo32.X_add_number = generic_bignum[0] + (generic_bignum[1] << 16);
      hi32.X_op = O_constant;
      hi32.X_add_number = generic_bignum[2] + (generic_bignum[3] << 16);
    }

  if (hi32.X_add_number == 0)
    freg = 0;
  else
    {
      int shift, bit;
      unsigned long hi, lo;

      if (hi32.X_add_number == (offsetT) 0xffffffff)
	{
	  if ((lo32.X_add_number & 0xffff8000) == 0xffff8000)
	    {
	      macro_build (&lo32, "addiu", "t,r,j", reg, 0, BFD_RELOC_LO16);
	      return;
	    }
	  if (lo32.X_add_number & 0x80000000)
	    {
	      macro_build (&lo32, "lui", "t,u", reg, BFD_RELOC_HI16);
	      if (lo32.X_add_number & 0xffff)
		macro_build (&lo32, "ori", "t,r,i", reg, reg, BFD_RELOC_LO16);
	      return;
	    }
	}

      /* Check for 16bit shifted constant.  We know that hi32 is
         non-zero, so start the mask on the first bit of the hi32
         value.  */
      shift = 17;
      do
	{
	  unsigned long himask, lomask;

	  if (shift < 32)
	    {
	      himask = 0xffff >> (32 - shift);
	      lomask = (0xffff << shift) & 0xffffffff;
	    }
	  else
	    {
	      himask = 0xffff << (shift - 32);
	      lomask = 0;
	    }
	  if ((hi32.X_add_number & ~(offsetT) himask) == 0
	      && (lo32.X_add_number & ~(offsetT) lomask) == 0)
	    {
	      expressionS tmp;

	      tmp.X_op = O_constant;
	      if (shift < 32)
		tmp.X_add_number = ((hi32.X_add_number << (32 - shift))
				    | (lo32.X_add_number >> shift));
	      else
		tmp.X_add_number = hi32.X_add_number >> (shift - 32);
	      macro_build (&tmp, "ori", "t,r,i", reg, 0, BFD_RELOC_LO16);
	      macro_build (NULL, (shift >= 32) ? "dsll32" : "dsll", "d,w,<",
			   reg, reg, (shift >= 32) ? shift - 32 : shift);
	      return;
	    }
	  ++shift;
	}
      while (shift <= (64 - 16));

      /* Find the bit number of the lowest one bit, and store the
         shifted value in hi/lo.  */
      hi = (unsigned long) (hi32.X_add_number & 0xffffffff);
      lo = (unsigned long) (lo32.X_add_number & 0xffffffff);
      if (lo != 0)
	{
	  bit = 0;
	  while ((lo & 1) == 0)
	    {
	      lo >>= 1;
	      ++bit;
	    }
	  lo |= (hi & (((unsigned long) 1 << bit) - 1)) << (32 - bit);
	  hi >>= bit;
	}
      else
	{
	  bit = 32;
	  while ((hi & 1) == 0)
	    {
	      hi >>= 1;
	      ++bit;
	    }
	  lo = hi;
	  hi = 0;
	}

      /* Optimize if the shifted value is a (power of 2) - 1.  */
      if ((hi == 0 && ((lo + 1) & lo) == 0)
	  || (lo == 0xffffffff && ((hi + 1) & hi) == 0))
	{
	  shift = COUNT_TOP_ZEROES ((unsigned int) hi32.X_add_number);
	  if (shift != 0)
	    {
	      expressionS tmp;

	      /* This instruction will set the register to be all
                 ones.  */
	      tmp.X_op = O_constant;
	      tmp.X_add_number = (offsetT) -1;
	      macro_build (&tmp, "addiu", "t,r,j", reg, 0, BFD_RELOC_LO16);
	      if (bit != 0)
		{
		  bit += shift;
		  macro_build (NULL, (bit >= 32) ? "dsll32" : "dsll", "d,w,<",
			       reg, reg, (bit >= 32) ? bit - 32 : bit);
		}
	      macro_build (NULL, (shift >= 32) ? "dsrl32" : "dsrl", "d,w,<",
			   reg, reg, (shift >= 32) ? shift - 32 : shift);
	      return;
	    }
	}

      /* Sign extend hi32 before calling load_register, because we can
         generally get better code when we load a sign extended value.  */
      if ((hi32.X_add_number & 0x80000000) != 0)
	hi32.X_add_number |= ~(offsetT) 0xffffffff;
      load_register (reg, &hi32, 0);
      freg = reg;
    }
  if ((lo32.X_add_number & 0xffff0000) == 0)
    {
      if (freg != 0)
	{
	  macro_build (NULL, "dsll32", "d,w,<", reg, freg, 0);
	  freg = reg;
	}
    }
  else
    {
      expressionS mid16;

      if ((freg == 0) && (lo32.X_add_number == (offsetT) 0xffffffff))
	{
	  macro_build (&lo32, "lui", "t,u", reg, BFD_RELOC_HI16);
	  macro_build (NULL, "dsrl32", "d,w,<", reg, reg, 0);
	  return;
	}

      if (freg != 0)
	{
	  macro_build (NULL, "dsll", "d,w,<", reg, freg, 16);
	  freg = reg;
	}
      mid16 = lo32;
      mid16.X_add_number >>= 16;
      macro_build (&mid16, "ori", "t,r,i", reg, freg, BFD_RELOC_LO16);
      macro_build (NULL, "dsll", "d,w,<", reg, reg, 16);
      freg = reg;
    }
  if ((lo32.X_add_number & 0xffff) != 0)
    macro_build (&lo32, "ori", "t,r,i", reg, freg, BFD_RELOC_LO16);
}

/* Load an address into a register.  */

static void
load_address (int reg, expressionS *ep, int *used_at)
{
  if (ep->X_op != O_constant
      && ep->X_op != O_symbol)
    {
      as_bad (_("expression too complex"));
      ep->X_op = O_constant;
    }

  if (ep->X_op == O_constant)
    {
      load_register (reg, ep, HAVE_64BIT_ADDRESSES);
      return;
    }

  if (mips_pic == NO_PIC)
    {
      /* If this is a reference to a GP relative symbol, we want
	   addiu	$reg,$gp,<sym>		(BFD_RELOC_GPREL16)
	 Otherwise we want
	   lui		$reg,<sym>		(BFD_RELOC_HI16_S)
	   addiu	$reg,$reg,<sym>		(BFD_RELOC_LO16)
	 If we have an addend, we always use the latter form.

	 With 64bit address space and a usable $at we want
	   lui		$reg,<sym>		(BFD_RELOC_MIPS_HIGHEST)
	   lui		$at,<sym>		(BFD_RELOC_HI16_S)
	   daddiu	$reg,<sym>		(BFD_RELOC_MIPS_HIGHER)
	   daddiu	$at,<sym>		(BFD_RELOC_LO16)
	   dsll32	$reg,0
	   daddu	$reg,$reg,$at

	 If $at is already in use, we use a path which is suboptimal
	 on superscalar processors.
	   lui		$reg,<sym>		(BFD_RELOC_MIPS_HIGHEST)
	   daddiu	$reg,<sym>		(BFD_RELOC_MIPS_HIGHER)
	   dsll		$reg,16
	   daddiu	$reg,<sym>		(BFD_RELOC_HI16_S)
	   dsll		$reg,16
	   daddiu	$reg,<sym>		(BFD_RELOC_LO16)
       */
      if (HAVE_64BIT_ADDRESSES)
	{
	  /* ??? We don't provide a GP-relative alternative for these macros.
	     It used not to be possible with the original relaxation code,
	     but it could be done now.  */

	  if (*used_at == 0 && ! mips_opts.noat)
	    {
	      macro_build (ep, "lui", "t,u", reg, BFD_RELOC_MIPS_HIGHEST);
	      macro_build (ep, "lui", "t,u", AT, BFD_RELOC_HI16_S);
	      macro_build (ep, "daddiu", "t,r,j", reg, reg,
			   BFD_RELOC_MIPS_HIGHER);
	      macro_build (ep, "daddiu", "t,r,j", AT, AT, BFD_RELOC_LO16);
	      macro_build (NULL, "dsll32", "d,w,<", reg, reg, 0);
	      macro_build (NULL, "daddu", "d,v,t", reg, reg, AT);
	      *used_at = 1;
	    }
	  else
	    {
	      macro_build (ep, "lui", "t,u", reg, BFD_RELOC_MIPS_HIGHEST);
	      macro_build (ep, "daddiu", "t,r,j", reg, reg,
			   BFD_RELOC_MIPS_HIGHER);
	      macro_build (NULL, "dsll", "d,w,<", reg, reg, 16);
	      macro_build (ep, "daddiu", "t,r,j", reg, reg, BFD_RELOC_HI16_S);
	      macro_build (NULL, "dsll", "d,w,<", reg, reg, 16);
	      macro_build (ep, "daddiu", "t,r,j", reg, reg, BFD_RELOC_LO16);
	    }
	}
      else
	{
	  if ((valueT) ep->X_add_number <= MAX_GPREL_OFFSET
	      && ! nopic_need_relax (ep->X_add_symbol, 1))
	    {
	      relax_start (ep->X_add_symbol);
	      macro_build (ep, ADDRESS_ADDI_INSN, "t,r,j", reg,
			   mips_gp_register, BFD_RELOC_GPREL16);
	      relax_switch ();
	    }
	  macro_build_lui (ep, reg);
	  macro_build (ep, ADDRESS_ADDI_INSN, "t,r,j",
		       reg, reg, BFD_RELOC_LO16);
	  if (mips_relax.sequence)
	    relax_end ();
	}
    }
  else if (mips_pic == SVR4_PIC && ! mips_big_got)
    {
      expressionS ex;

      /* If this is a reference to an external symbol, we want
	   lw		$reg,<sym>($gp)		(BFD_RELOC_MIPS_GOT16)
	 Otherwise we want
	   lw		$reg,<sym>($gp)		(BFD_RELOC_MIPS_GOT16)
	   nop
	   addiu	$reg,$reg,<sym>		(BFD_RELOC_LO16)
	 If there is a constant, it must be added in after.

	 If we have NewABI, we want
	   lw		$reg,<sym+cst>($gp)	(BFD_RELOC_MIPS_GOT_DISP)
         unless we're referencing a global symbol with a non-zero
         offset, in which case cst must be added separately.  */
      if (HAVE_NEWABI)
	{
	  if (ep->X_add_number)
	    {
	      ex.X_add_number = ep->X_add_number;
	      ep->X_add_number = 0;
	      relax_start (ep->X_add_symbol);
	      macro_build (ep, ADDRESS_LOAD_INSN, "t,o(b)", reg,
			   BFD_RELOC_MIPS_GOT_DISP, mips_gp_register);
	      if (ex.X_add_number < -0x8000 || ex.X_add_number >= 0x8000)
		as_bad (_("PIC code offset overflow (max 16 signed bits)"));
	      ex.X_op = O_constant;
	      macro_build (&ex, ADDRESS_ADDI_INSN, "t,r,j",
			   reg, reg, BFD_RELOC_LO16);
	      ep->X_add_number = ex.X_add_number;
	      relax_switch ();
	    }
	  macro_build (ep, ADDRESS_LOAD_INSN, "t,o(b)", reg,
		       BFD_RELOC_MIPS_GOT_DISP, mips_gp_register);
	  if (mips_relax.sequence)
	    relax_end ();
	}
      else
	{
	  ex.X_add_number = ep->X_add_number;
	  ep->X_add_number = 0;
	  macro_build (ep, ADDRESS_LOAD_INSN, "t,o(b)", reg,
		       BFD_RELOC_MIPS_GOT16, mips_gp_register);
	  macro_build (NULL, "nop", "");
	  relax_start (ep->X_add_symbol);
	  relax_switch ();
	  macro_build (ep, ADDRESS_ADDI_INSN, "t,r,j", reg, reg,
		       BFD_RELOC_LO16);
	  relax_end ();

	  if (ex.X_add_number != 0)
	    {
	      if (ex.X_add_number < -0x8000 || ex.X_add_number >= 0x8000)
		as_bad (_("PIC code offset overflow (max 16 signed bits)"));
	      ex.X_op = O_constant;
	      macro_build (&ex, ADDRESS_ADDI_INSN, "t,r,j",
			   reg, reg, BFD_RELOC_LO16);
	    }
	}
    }
  else if (mips_pic == SVR4_PIC)
    {
      expressionS ex;

      /* This is the large GOT case.  If this is a reference to an
	 external symbol, we want
	   lui		$reg,<sym>		(BFD_RELOC_MIPS_GOT_HI16)
	   addu		$reg,$reg,$gp
	   lw		$reg,<sym>($reg)	(BFD_RELOC_MIPS_GOT_LO16)

	 Otherwise, for a reference to a local symbol in old ABI, we want
	   lw		$reg,<sym>($gp)		(BFD_RELOC_MIPS_GOT16)
	   nop
	   addiu	$reg,$reg,<sym>		(BFD_RELOC_LO16)
	 If there is a constant, it must be added in after.

	 In the NewABI, for local symbols, with or without offsets, we want:
	   lw		$reg,<sym>($gp)		(BFD_RELOC_MIPS_GOT_PAGE)
	   addiu	$reg,$reg,<sym>		(BFD_RELOC_MIPS_GOT_OFST)
      */
      if (HAVE_NEWABI)
	{
	  ex.X_add_number = ep->X_add_number;
	  ep->X_add_number = 0;
	  relax_start (ep->X_add_symbol);
	  macro_build (ep, "lui", "t,u", reg, BFD_RELOC_MIPS_GOT_HI16);
	  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
		       reg, reg, mips_gp_register);
	  macro_build (ep, ADDRESS_LOAD_INSN, "t,o(b)",
		       reg, BFD_RELOC_MIPS_GOT_LO16, reg);
	  if (ex.X_add_number < -0x8000 || ex.X_add_number >= 0x8000)
	    as_bad (_("PIC code offset overflow (max 16 signed bits)"));
	  else if (ex.X_add_number)
	    {
	      ex.X_op = O_constant;
	      macro_build (&ex, ADDRESS_ADDI_INSN, "t,r,j", reg, reg,
			   BFD_RELOC_LO16);
	    }

	  ep->X_add_number = ex.X_add_number;
	  relax_switch ();
	  macro_build (ep, ADDRESS_LOAD_INSN, "t,o(b)", reg,
		       BFD_RELOC_MIPS_GOT_PAGE, mips_gp_register);
	  macro_build (ep, ADDRESS_ADDI_INSN, "t,r,j", reg, reg,
		       BFD_RELOC_MIPS_GOT_OFST);
	  relax_end ();
	}
      else
	{
	  ex.X_add_number = ep->X_add_number;
	  ep->X_add_number = 0;
	  relax_start (ep->X_add_symbol);
	  macro_build (ep, "lui", "t,u", reg, BFD_RELOC_MIPS_GOT_HI16);
	  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
		       reg, reg, mips_gp_register);
	  macro_build (ep, ADDRESS_LOAD_INSN, "t,o(b)",
		       reg, BFD_RELOC_MIPS_GOT_LO16, reg);
	  relax_switch ();
	  if (reg_needs_delay (mips_gp_register))
	    {
	      /* We need a nop before loading from $gp.  This special
		 check is required because the lui which starts the main
		 instruction stream does not refer to $gp, and so will not
		 insert the nop which may be required.  */
	      macro_build (NULL, "nop", "");
	    }
	  macro_build (ep, ADDRESS_LOAD_INSN, "t,o(b)", reg,
		       BFD_RELOC_MIPS_GOT16, mips_gp_register);
	  macro_build (NULL, "nop", "");
	  macro_build (ep, ADDRESS_ADDI_INSN, "t,r,j", reg, reg,
		       BFD_RELOC_LO16);
	  relax_end ();

	  if (ex.X_add_number != 0)
	    {
	      if (ex.X_add_number < -0x8000 || ex.X_add_number >= 0x8000)
		as_bad (_("PIC code offset overflow (max 16 signed bits)"));
	      ex.X_op = O_constant;
	      macro_build (&ex, ADDRESS_ADDI_INSN, "t,r,j", reg, reg,
			   BFD_RELOC_LO16);
	    }
	}
    }
  else if (mips_pic == EMBEDDED_PIC)
    {
      /* We always do
	   addiu	$reg,$gp,<sym>		(BFD_RELOC_GPREL16)
       */
      macro_build (ep, ADDRESS_ADDI_INSN, "t,r,j",
		   reg, mips_gp_register, BFD_RELOC_GPREL16);
    }
  else
    abort ();
}

/* Move the contents of register SOURCE into register DEST.  */

static void
move_register (int dest, int source)
{
  macro_build (NULL, HAVE_32BIT_GPRS ? "addu" : "daddu", "d,v,t",
	       dest, source, 0);
}

/* Emit an SVR4 PIC sequence to load address LOCAL into DEST, where
   LOCAL is the sum of a symbol and a 16-bit or 32-bit displacement.
   The two alternatives are:

   Global symbol		Local sybmol
   -------------		------------
   lw DEST,%got(SYMBOL)		lw DEST,%got(SYMBOL + OFFSET)
   ...				...
   addiu DEST,DEST,OFFSET	addiu DEST,DEST,%lo(SYMBOL + OFFSET)

   load_got_offset emits the first instruction and add_got_offset
   emits the second for a 16-bit offset or add_got_offset_hilo emits
   a sequence to add a 32-bit offset using a scratch register.  */

static void
load_got_offset (int dest, expressionS *local)
{
  expressionS global;

  global = *local;
  global.X_add_number = 0;

  relax_start (local->X_add_symbol);
  macro_build (&global, ADDRESS_LOAD_INSN, "t,o(b)", dest,
	       BFD_RELOC_MIPS_GOT16, mips_gp_register);
  relax_switch ();
  macro_build (local, ADDRESS_LOAD_INSN, "t,o(b)", dest,
	       BFD_RELOC_MIPS_GOT16, mips_gp_register);
  relax_end ();
}

static void
add_got_offset (int dest, expressionS *local)
{
  expressionS global;

  global.X_op = O_constant;
  global.X_op_symbol = NULL;
  global.X_add_symbol = NULL;
  global.X_add_number = local->X_add_number;

  relax_start (local->X_add_symbol);
  macro_build (&global, ADDRESS_ADDI_INSN, "t,r,j",
	       dest, dest, BFD_RELOC_LO16);
  relax_switch ();
  macro_build (local, ADDRESS_ADDI_INSN, "t,r,j", dest, dest, BFD_RELOC_LO16);
  relax_end ();
}

static void
add_got_offset_hilo (int dest, expressionS *local, int tmp)
{
  expressionS global;
  int hold_mips_optimize;

  global.X_op = O_constant;
  global.X_op_symbol = NULL;
  global.X_add_symbol = NULL;
  global.X_add_number = local->X_add_number;

  relax_start (local->X_add_symbol);
  load_register (tmp, &global, HAVE_64BIT_ADDRESSES);
  relax_switch ();
  /* Set mips_optimize around the lui instruction to avoid
     inserting an unnecessary nop after the lw.  */
  hold_mips_optimize = mips_optimize;
  mips_optimize = 2;
  macro_build_lui (&global, tmp);
  mips_optimize = hold_mips_optimize;
  macro_build (local, ADDRESS_ADDI_INSN, "t,r,j", tmp, tmp, BFD_RELOC_LO16);
  relax_end ();

  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", dest, dest, tmp);
}

/*
 *			Build macros
 *   This routine implements the seemingly endless macro or synthesized
 * instructions and addressing modes in the mips assembly language. Many
 * of these macros are simple and are similar to each other. These could
 * probably be handled by some kind of table or grammar approach instead of
 * this verbose method. Others are not simple macros but are more like
 * optimizing code generation.
 *   One interesting optimization is when several store macros appear
 * consecutively that would load AT with the upper half of the same address.
 * The ensuing load upper instructions are ommited. This implies some kind
 * of global optimization. We currently only optimize within a single macro.
 *   For many of the load and store macros if the address is specified as a
 * constant expression in the first 64k of memory (ie ld $2,0x4000c) we
 * first load register 'at' with zero and use it as the base register. The
 * mips assembler simply uses register $zero. Just one tiny optimization
 * we're missing.
 */
static void
macro (struct mips_cl_insn *ip)
{
  register int treg, sreg, dreg, breg;
  int tempreg;
  int mask;
  int used_at = 0;
  expressionS expr1;
  const char *s;
  const char *s2;
  const char *fmt;
  int likely = 0;
  int dbl = 0;
  int coproc = 0;
  int lr = 0;
  int imm = 0;
  int call = 0;
  int off;
  offsetT maxnum;
  bfd_reloc_code_real_type r;
  int hold_mips_optimize;

  assert (! mips_opts.mips16);

  treg = (ip->insn_opcode >> 16) & 0x1f;
  dreg = (ip->insn_opcode >> 11) & 0x1f;
  sreg = breg = (ip->insn_opcode >> 21) & 0x1f;
  mask = ip->insn_mo->mask;

  expr1.X_op = O_constant;
  expr1.X_op_symbol = NULL;
  expr1.X_add_symbol = NULL;
  expr1.X_add_number = 1;

  switch (mask)
    {
    case M_DABS:
      dbl = 1;
    case M_ABS:
      /* bgez $a0,.+12
	 move v0,$a0
	 sub v0,$zero,$a0
	 */

      mips_emit_delays (TRUE);
      ++mips_opts.noreorder;
      mips_any_noreorder = 1;

      expr1.X_add_number = 8;
      macro_build (&expr1, "bgez", "s,p", sreg);
      if (dreg == sreg)
	macro_build (NULL, "nop", "", 0);
      else
	move_register (dreg, sreg);
      macro_build (NULL, dbl ? "dsub" : "sub", "d,v,t", dreg, 0, sreg);

      --mips_opts.noreorder;
      return;

    case M_ADD_I:
      s = "addi";
      s2 = "add";
      goto do_addi;
    case M_ADDU_I:
      s = "addiu";
      s2 = "addu";
      goto do_addi;
    case M_DADD_I:
      dbl = 1;
      s = "daddi";
      s2 = "dadd";
      goto do_addi;
    case M_DADDU_I:
      dbl = 1;
      s = "daddiu";
      s2 = "daddu";
    do_addi:
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number >= -0x8000
	  && imm_expr.X_add_number < 0x8000)
	{
	  macro_build (&imm_expr, s, "t,r,j", treg, sreg, BFD_RELOC_LO16);
	  return;
	}
      load_register (AT, &imm_expr, dbl);
      macro_build (NULL, s2, "d,v,t", treg, sreg, AT);
      break;

    case M_AND_I:
      s = "andi";
      s2 = "and";
      goto do_bit;
    case M_OR_I:
      s = "ori";
      s2 = "or";
      goto do_bit;
    case M_NOR_I:
      s = "";
      s2 = "nor";
      goto do_bit;
    case M_XOR_I:
      s = "xori";
      s2 = "xor";
    do_bit:
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number >= 0
	  && imm_expr.X_add_number < 0x10000)
	{
	  if (mask != M_NOR_I)
	    macro_build (&imm_expr, s, "t,r,i", treg, sreg, BFD_RELOC_LO16);
	  else
	    {
	      macro_build (&imm_expr, "ori", "t,r,i",
			   treg, sreg, BFD_RELOC_LO16);
	      macro_build (NULL, "nor", "d,v,t", treg, treg, 0);
	    }
	  return;
	}

      load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
      macro_build (NULL, s2, "d,v,t", treg, sreg, AT);
      break;

    case M_BEQ_I:
      s = "beq";
      goto beq_i;
    case M_BEQL_I:
      s = "beql";
      likely = 1;
      goto beq_i;
    case M_BNE_I:
      s = "bne";
      goto beq_i;
    case M_BNEL_I:
      s = "bnel";
      likely = 1;
    beq_i:
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 0)
	{
	  macro_build (&offset_expr, s, "s,t,p", sreg, 0);
	  return;
	}
      load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
      macro_build (&offset_expr, s, "s,t,p", sreg, AT);
      break;

    case M_BGEL:
      likely = 1;
    case M_BGE:
      if (treg == 0)
	{
	  macro_build (&offset_expr, likely ? "bgezl" : "bgez", "s,p", sreg);
	  return;
	}
      if (sreg == 0)
	{
	  macro_build (&offset_expr, likely ? "blezl" : "blez", "s,p", treg);
	  return;
	}
      macro_build (NULL, "slt", "d,v,t", AT, sreg, treg);
      macro_build (&offset_expr, likely ? "beql" : "beq", "s,t,p", AT, 0);
      break;

    case M_BGTL_I:
      likely = 1;
    case M_BGT_I:
      /* check for > max integer */
      maxnum = 0x7fffffff;
      if (HAVE_64BIT_GPRS && sizeof (maxnum) > 4)
	{
	  maxnum <<= 16;
	  maxnum |= 0xffff;
	  maxnum <<= 16;
	  maxnum |= 0xffff;
	}
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number >= maxnum
	  && (HAVE_32BIT_GPRS || sizeof (maxnum) > 4))
	{
	do_false:
	  /* result is always false */
	  if (! likely)
	    macro_build (NULL, "nop", "", 0);
	  else
	    macro_build (&offset_expr, "bnel", "s,t,p", 0, 0);
	  return;
	}
      if (imm_expr.X_op != O_constant)
	as_bad (_("Unsupported large constant"));
      ++imm_expr.X_add_number;
      /* FALLTHROUGH */
    case M_BGE_I:
    case M_BGEL_I:
      if (mask == M_BGEL_I)
	likely = 1;
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 0)
	{
	  macro_build (&offset_expr, likely ? "bgezl" : "bgez", "s,p", sreg);
	  return;
	}
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 1)
	{
	  macro_build (&offset_expr, likely ? "bgtzl" : "bgtz", "s,p", sreg);
	  return;
	}
      maxnum = 0x7fffffff;
      if (HAVE_64BIT_GPRS && sizeof (maxnum) > 4)
	{
	  maxnum <<= 16;
	  maxnum |= 0xffff;
	  maxnum <<= 16;
	  maxnum |= 0xffff;
	}
      maxnum = - maxnum - 1;
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number <= maxnum
	  && (HAVE_32BIT_GPRS || sizeof (maxnum) > 4))
	{
	do_true:
	  /* result is always true */
	  as_warn (_("Branch %s is always true"), ip->insn_mo->name);
	  macro_build (&offset_expr, "b", "p");
	  return;
	}
      set_at (sreg, 0);
      macro_build (&offset_expr, likely ? "beql" : "beq", "s,t,p", AT, 0);
      break;

    case M_BGEUL:
      likely = 1;
    case M_BGEU:
      if (treg == 0)
	goto do_true;
      if (sreg == 0)
	{
	  macro_build (&offset_expr, likely ? "beql" : "beq",
		       "s,t,p", 0, treg);
	  return;
	}
      macro_build (NULL, "sltu", "d,v,t", AT, sreg, treg);
      macro_build (&offset_expr, likely ? "beql" : "beq", "s,t,p", AT, 0);
      break;

    case M_BGTUL_I:
      likely = 1;
    case M_BGTU_I:
      if (sreg == 0
	  || (HAVE_32BIT_GPRS
	      && imm_expr.X_op == O_constant
	      && imm_expr.X_add_number == (offsetT) 0xffffffff))
	goto do_false;
      if (imm_expr.X_op != O_constant)
	as_bad (_("Unsupported large constant"));
      ++imm_expr.X_add_number;
      /* FALLTHROUGH */
    case M_BGEU_I:
    case M_BGEUL_I:
      if (mask == M_BGEUL_I)
	likely = 1;
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 0)
	goto do_true;
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 1)
	{
	  macro_build (&offset_expr, likely ? "bnel" : "bne",
		       "s,t,p", sreg, 0);
	  return;
	}
      set_at (sreg, 1);
      macro_build (&offset_expr, likely ? "beql" : "beq", "s,t,p", AT, 0);
      break;

    case M_BGTL:
      likely = 1;
    case M_BGT:
      if (treg == 0)
	{
	  macro_build (&offset_expr, likely ? "bgtzl" : "bgtz", "s,p", sreg);
	  return;
	}
      if (sreg == 0)
	{
	  macro_build (&offset_expr, likely ? "bltzl" : "bltz", "s,p", treg);
	  return;
	}
      macro_build (NULL, "slt", "d,v,t", AT, treg, sreg);
      macro_build (&offset_expr, likely ? "bnel" : "bne", "s,t,p", AT, 0);
      break;

    case M_BGTUL:
      likely = 1;
    case M_BGTU:
      if (treg == 0)
	{
	  macro_build (&offset_expr, likely ? "bnel" : "bne",
		       "s,t,p", sreg, 0);
	  return;
	}
      if (sreg == 0)
	goto do_false;
      macro_build (NULL, "sltu", "d,v,t", AT, treg, sreg);
      macro_build (&offset_expr, likely ? "bnel" : "bne", "s,t,p", AT, 0);
      break;

    case M_BLEL:
      likely = 1;
    case M_BLE:
      if (treg == 0)
	{
	  macro_build (&offset_expr, likely ? "blezl" : "blez", "s,p", sreg);
	  return;
	}
      if (sreg == 0)
	{
	  macro_build (&offset_expr, likely ? "bgezl" : "bgez", "s,p", treg);
	  return;
	}
      macro_build (NULL, "slt", "d,v,t", AT, treg, sreg);
      macro_build (&offset_expr, likely ? "beql" : "beq", "s,t,p", AT, 0);
      break;

    case M_BLEL_I:
      likely = 1;
    case M_BLE_I:
      maxnum = 0x7fffffff;
      if (HAVE_64BIT_GPRS && sizeof (maxnum) > 4)
	{
	  maxnum <<= 16;
	  maxnum |= 0xffff;
	  maxnum <<= 16;
	  maxnum |= 0xffff;
	}
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number >= maxnum
	  && (HAVE_32BIT_GPRS || sizeof (maxnum) > 4))
	goto do_true;
      if (imm_expr.X_op != O_constant)
	as_bad (_("Unsupported large constant"));
      ++imm_expr.X_add_number;
      /* FALLTHROUGH */
    case M_BLT_I:
    case M_BLTL_I:
      if (mask == M_BLTL_I)
	likely = 1;
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 0)
	{
	  macro_build (&offset_expr, likely ? "bltzl" : "bltz", "s,p", sreg);
	  return;
	}
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 1)
	{
	  macro_build (&offset_expr, likely ? "blezl" : "blez", "s,p", sreg);
	  return;
	}
      set_at (sreg, 0);
      macro_build (&offset_expr, likely ? "bnel" : "bne", "s,t,p", AT, 0);
      break;

    case M_BLEUL:
      likely = 1;
    case M_BLEU:
      if (treg == 0)
	{
	  macro_build (&offset_expr, likely ? "beql" : "beq",
		       "s,t,p", sreg, 0);
	  return;
	}
      if (sreg == 0)
	goto do_true;
      macro_build (NULL, "sltu", "d,v,t", AT, treg, sreg);
      macro_build (&offset_expr, likely ? "beql" : "beq", "s,t,p", AT, 0);
      break;

    case M_BLEUL_I:
      likely = 1;
    case M_BLEU_I:
      if (sreg == 0
	  || (HAVE_32BIT_GPRS
	      && imm_expr.X_op == O_constant
	      && imm_expr.X_add_number == (offsetT) 0xffffffff))
	goto do_true;
      if (imm_expr.X_op != O_constant)
	as_bad (_("Unsupported large constant"));
      ++imm_expr.X_add_number;
      /* FALLTHROUGH */
    case M_BLTU_I:
    case M_BLTUL_I:
      if (mask == M_BLTUL_I)
	likely = 1;
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 0)
	goto do_false;
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 1)
	{
	  macro_build (&offset_expr, likely ? "beql" : "beq",
		       "s,t,p", sreg, 0);
	  return;
	}
      set_at (sreg, 1);
      macro_build (&offset_expr, likely ? "bnel" : "bne", "s,t,p", AT, 0);
      break;

    case M_BLTL:
      likely = 1;
    case M_BLT:
      if (treg == 0)
	{
	  macro_build (&offset_expr, likely ? "bltzl" : "bltz", "s,p", sreg);
	  return;
	}
      if (sreg == 0)
	{
	  macro_build (&offset_expr, likely ? "bgtzl" : "bgtz", "s,p", treg);
	  return;
	}
      macro_build (NULL, "slt", "d,v,t", AT, sreg, treg);
      macro_build (&offset_expr, likely ? "bnel" : "bne", "s,t,p", AT, 0);
      break;

    case M_BLTUL:
      likely = 1;
    case M_BLTU:
      if (treg == 0)
	goto do_false;
      if (sreg == 0)
	{
	  macro_build (&offset_expr, likely ? "bnel" : "bne",
		       "s,t,p", 0, treg);
	  return;
	}
      macro_build (NULL, "sltu", "d,v,t", AT, sreg, treg);
      macro_build (&offset_expr, likely ? "bnel" : "bne", "s,t,p", AT, 0);
      break;

    case M_DEXT:
      {
	unsigned long pos;
	unsigned long size;

        if (imm_expr.X_op != O_constant || imm2_expr.X_op != O_constant)
	  {
	    as_bad (_("Unsupported large constant"));
	    pos = size = 1;
	  }
	else
	  {
	    pos = (unsigned long) imm_expr.X_add_number;
	    size = (unsigned long) imm2_expr.X_add_number;
	  }

	if (pos > 63)
	  {
	    as_bad (_("Improper position (%lu)"), pos);
	    pos = 1;
	  }
        if (size == 0 || size > 64
	    || (pos + size - 1) > 63)
	  {
	    as_bad (_("Improper extract size (%lu, position %lu)"),
		    size, pos);
	    size = 1;
	  }

	if (size <= 32 && pos < 32)
	  {
	    s = "dext";
	    fmt = "t,r,+A,+C";
	  }
	else if (size <= 32)
	  {
	    s = "dextu";
	    fmt = "t,r,+E,+H";
	  }
	else
	  {
	    s = "dextm";
	    fmt = "t,r,+A,+G";
	  }
	macro_build ((expressionS *) NULL, s, fmt, treg, sreg, pos, size - 1);
      }
      return;

    case M_DINS:
      {
	unsigned long pos;
	unsigned long size;

        if (imm_expr.X_op != O_constant || imm2_expr.X_op != O_constant)
	  {
	    as_bad (_("Unsupported large constant"));
	    pos = size = 1;
	  }
	else
	  {
	    pos = (unsigned long) imm_expr.X_add_number;
	    size = (unsigned long) imm2_expr.X_add_number;
	  }

	if (pos > 63)
	  {
	    as_bad (_("Improper position (%lu)"), pos);
	    pos = 1;
	  }
        if (size == 0 || size > 64
	    || (pos + size - 1) > 63)
	  {
	    as_bad (_("Improper insert size (%lu, position %lu)"),
		    size, pos);
	    size = 1;
	  }

	if (pos < 32 && (pos + size - 1) < 32)
	  {
	    s = "dins";
	    fmt = "t,r,+A,+B";
	  }
	else if (pos >= 32)
	  {
	    s = "dinsu";
	    fmt = "t,r,+E,+F";
	  }
	else
	  {
	    s = "dinsm";
	    fmt = "t,r,+A,+F";
	  }
	macro_build ((expressionS *) NULL, s, fmt, treg, sreg, pos,
		     pos + size - 1);
      }
      return;

    case M_DDIV_3:
      dbl = 1;
    case M_DIV_3:
      s = "mflo";
      goto do_div3;
    case M_DREM_3:
      dbl = 1;
    case M_REM_3:
      s = "mfhi";
    do_div3:
      if (treg == 0)
	{
	  as_warn (_("Divide by zero."));
	  if (mips_trap)
	    macro_build (NULL, "teq", "s,t,q", 0, 0, 7);
	  else
	    macro_build (NULL, "break", "c", 7);
	  return;
	}

      mips_emit_delays (TRUE);
      ++mips_opts.noreorder;
      mips_any_noreorder = 1;
      if (mips_trap)
	{
	  macro_build (NULL, "teq", "s,t,q", treg, 0, 7);
	  macro_build (NULL, dbl ? "ddiv" : "div", "z,s,t", sreg, treg);
	}
      else
	{
	  expr1.X_add_number = 8;
	  macro_build (&expr1, "bne", "s,t,p", treg, 0);
	  macro_build (NULL, dbl ? "ddiv" : "div", "z,s,t", sreg, treg);
	  macro_build (NULL, "break", "c", 7);
	}
      expr1.X_add_number = -1;
      load_register (AT, &expr1, dbl);
      expr1.X_add_number = mips_trap ? (dbl ? 12 : 8) : (dbl ? 20 : 16);
      macro_build (&expr1, "bne", "s,t,p", treg, AT);
      if (dbl)
	{
	  expr1.X_add_number = 1;
	  load_register (AT, &expr1, dbl);
	  macro_build (NULL, "dsll32", "d,w,<", AT, AT, 31);
	}
      else
	{
	  expr1.X_add_number = 0x80000000;
	  macro_build (&expr1, "lui", "t,u", AT, BFD_RELOC_HI16);
	}
      if (mips_trap)
	{
	  macro_build (NULL, "teq", "s,t,q", sreg, AT, 6);
	  /* We want to close the noreorder block as soon as possible, so
	     that later insns are available for delay slot filling.  */
	  --mips_opts.noreorder;
	}
      else
	{
	  expr1.X_add_number = 8;
	  macro_build (&expr1, "bne", "s,t,p", sreg, AT);
	  macro_build (NULL, "nop", "", 0);

	  /* We want to close the noreorder block as soon as possible, so
	     that later insns are available for delay slot filling.  */
	  --mips_opts.noreorder;

	  macro_build (NULL, "break", "c", 6);
	}
      macro_build (NULL, s, "d", dreg);
      break;

    case M_DIV_3I:
      s = "div";
      s2 = "mflo";
      goto do_divi;
    case M_DIVU_3I:
      s = "divu";
      s2 = "mflo";
      goto do_divi;
    case M_REM_3I:
      s = "div";
      s2 = "mfhi";
      goto do_divi;
    case M_REMU_3I:
      s = "divu";
      s2 = "mfhi";
      goto do_divi;
    case M_DDIV_3I:
      dbl = 1;
      s = "ddiv";
      s2 = "mflo";
      goto do_divi;
    case M_DDIVU_3I:
      dbl = 1;
      s = "ddivu";
      s2 = "mflo";
      goto do_divi;
    case M_DREM_3I:
      dbl = 1;
      s = "ddiv";
      s2 = "mfhi";
      goto do_divi;
    case M_DREMU_3I:
      dbl = 1;
      s = "ddivu";
      s2 = "mfhi";
    do_divi:
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 0)
	{
	  as_warn (_("Divide by zero."));
	  if (mips_trap)
	    macro_build (NULL, "teq", "s,t,q", 0, 0, 7);
	  else
	    macro_build (NULL, "break", "c", 7);
	  return;
	}
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 1)
	{
	  if (strcmp (s2, "mflo") == 0)
	    move_register (dreg, sreg);
	  else
	    move_register (dreg, 0);
	  return;
	}
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number == -1
	  && s[strlen (s) - 1] != 'u')
	{
	  if (strcmp (s2, "mflo") == 0)
	    {
	      macro_build (NULL, dbl ? "dneg" : "neg", "d,w", dreg, sreg);
	    }
	  else
	    move_register (dreg, 0);
	  return;
	}

      load_register (AT, &imm_expr, dbl);
      macro_build (NULL, s, "z,s,t", sreg, AT);
      macro_build (NULL, s2, "d", dreg);
      break;

    case M_DIVU_3:
      s = "divu";
      s2 = "mflo";
      goto do_divu3;
    case M_REMU_3:
      s = "divu";
      s2 = "mfhi";
      goto do_divu3;
    case M_DDIVU_3:
      s = "ddivu";
      s2 = "mflo";
      goto do_divu3;
    case M_DREMU_3:
      s = "ddivu";
      s2 = "mfhi";
    do_divu3:
      mips_emit_delays (TRUE);
      ++mips_opts.noreorder;
      mips_any_noreorder = 1;
      if (mips_trap)
	{
	  macro_build (NULL, "teq", "s,t,q", treg, 0, 7);
	  macro_build (NULL, s, "z,s,t", sreg, treg);
	  /* We want to close the noreorder block as soon as possible, so
	     that later insns are available for delay slot filling.  */
	  --mips_opts.noreorder;
	}
      else
	{
	  expr1.X_add_number = 8;
	  macro_build (&expr1, "bne", "s,t,p", treg, 0);
	  macro_build (NULL, s, "z,s,t", sreg, treg);

	  /* We want to close the noreorder block as soon as possible, so
	     that later insns are available for delay slot filling.  */
	  --mips_opts.noreorder;
	  macro_build (NULL, "break", "c", 7);
	}
      macro_build (NULL, s2, "d", dreg);
      return;

    case M_DLCA_AB:
      dbl = 1;
    case M_LCA_AB:
      call = 1;
      goto do_la;
    case M_DLA_AB:
      dbl = 1;
    case M_LA_AB:
    do_la:
      /* Load the address of a symbol into a register.  If breg is not
	 zero, we then add a base register to it.  */

      if (dbl && HAVE_32BIT_GPRS)
	as_warn (_("dla used to load 32-bit register"));

      if (! dbl && HAVE_64BIT_OBJECTS)
	as_warn (_("la used to load 64-bit address"));

      if (offset_expr.X_op == O_constant
	  && offset_expr.X_add_number >= -0x8000
	  && offset_expr.X_add_number < 0x8000)
	{
	  macro_build (&offset_expr,
		       (dbl || HAVE_64BIT_ADDRESSES) ? "daddiu" : "addiu",
		       "t,r,j", treg, sreg, BFD_RELOC_LO16);
	  return;
	}

      if (treg == breg)
	{
	  tempreg = AT;
	  used_at = 1;
	}
      else
	{
	  tempreg = treg;
	  used_at = 0;
	}

      /* When generating embedded PIC code, we permit expressions of
	 the form
	   la	$treg,foo-bar
	   la	$treg,foo-bar($breg)
	 where bar is an address in the current section.  These are used
	 when getting the addresses of functions.  We don't permit
	 X_add_number to be non-zero, because if the symbol is
	 external the relaxing code needs to know that any addend is
	 purely the offset to X_op_symbol.  */
      if (mips_pic == EMBEDDED_PIC
	  && offset_expr.X_op == O_subtract
	  && (symbol_constant_p (offset_expr.X_op_symbol)
	      ? S_GET_SEGMENT (offset_expr.X_op_symbol) == now_seg
	      : (symbol_equated_p (offset_expr.X_op_symbol)
		 && (S_GET_SEGMENT
		     (symbol_get_value_expression (offset_expr.X_op_symbol)
		      ->X_add_symbol)
		     == now_seg)))
	  && (offset_expr.X_add_number == 0
	      || OUTPUT_FLAVOR == bfd_target_elf_flavour))
	{
	  if (breg == 0)
	    {
	      tempreg = treg;
	      used_at = 0;
	      macro_build (&offset_expr, "lui", "t,u",
			   tempreg, BFD_RELOC_PCREL_HI16_S);
	    }
	  else
	    {
	      macro_build (&offset_expr, "lui", "t,u",
			   tempreg, BFD_RELOC_PCREL_HI16_S);
	      macro_build (NULL,
			   (dbl || HAVE_64BIT_ADDRESSES) ? "daddu" : "addu",
			   "d,v,t", tempreg, tempreg, breg);
	    }
	  macro_build (&offset_expr,
		       (dbl || HAVE_64BIT_ADDRESSES) ? "daddiu" : "addiu",
		       "t,r,j", treg, tempreg, BFD_RELOC_PCREL_LO16);
	  if (! used_at)
	    return;
	  break;
	}

      if (offset_expr.X_op != O_symbol
	  && offset_expr.X_op != O_constant)
	{
	  as_bad (_("expression too complex"));
	  offset_expr.X_op = O_constant;
	}

      if (offset_expr.X_op == O_constant)
	load_register (tempreg, &offset_expr,
		       ((mips_pic == EMBEDDED_PIC || mips_pic == NO_PIC)
			? (dbl || HAVE_64BIT_ADDRESSES)
			: HAVE_64BIT_ADDRESSES));
      else if (mips_pic == NO_PIC)
	{
	  /* If this is a reference to a GP relative symbol, we want
	       addiu	$tempreg,$gp,<sym>	(BFD_RELOC_GPREL16)
	     Otherwise we want
	       lui	$tempreg,<sym>		(BFD_RELOC_HI16_S)
	       addiu	$tempreg,$tempreg,<sym>	(BFD_RELOC_LO16)
	     If we have a constant, we need two instructions anyhow,
	     so we may as well always use the latter form.

	    With 64bit address space and a usable $at we want
	      lui	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHEST)
	      lui	$at,<sym>		(BFD_RELOC_HI16_S)
	      daddiu	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHER)
	      daddiu	$at,<sym>		(BFD_RELOC_LO16)
	      dsll32	$tempreg,0
	      daddu	$tempreg,$tempreg,$at

	    If $at is already in use, we use a path which is suboptimal
	    on superscalar processors.
	      lui	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHEST)
	      daddiu	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHER)
	      dsll	$tempreg,16
	      daddiu	$tempreg,<sym>		(BFD_RELOC_HI16_S)
	      dsll	$tempreg,16
	      daddiu	$tempreg,<sym>		(BFD_RELOC_LO16)
	  */
	  if (HAVE_64BIT_ADDRESSES)
	    {
	      /* ??? We don't provide a GP-relative alternative for
		 these macros.  It used not to be possible with the
		 original relaxation code, but it could be done now.  */

	      if (used_at == 0 && ! mips_opts.noat)
		{
		  macro_build (&offset_expr, "lui", "t,u",
			       tempreg, BFD_RELOC_MIPS_HIGHEST);
		  macro_build (&offset_expr, "lui", "t,u",
			       AT, BFD_RELOC_HI16_S);
		  macro_build (&offset_expr, "daddiu", "t,r,j",
			       tempreg, tempreg, BFD_RELOC_MIPS_HIGHER);
		  macro_build (&offset_expr, "daddiu", "t,r,j",
			       AT, AT, BFD_RELOC_LO16);
		  macro_build (NULL, "dsll32", "d,w,<", tempreg, tempreg, 0);
		  macro_build (NULL, "daddu", "d,v,t", tempreg, tempreg, AT);
		  used_at = 1;
		}
	      else
		{
		  macro_build (&offset_expr, "lui", "t,u",
			       tempreg, BFD_RELOC_MIPS_HIGHEST);
		  macro_build (&offset_expr, "daddiu", "t,r,j",
			       tempreg, tempreg, BFD_RELOC_MIPS_HIGHER);
		  macro_build (NULL, "dsll", "d,w,<", tempreg, tempreg, 16);
		  macro_build (&offset_expr, "daddiu", "t,r,j",
			       tempreg, tempreg, BFD_RELOC_HI16_S);
		  macro_build (NULL, "dsll", "d,w,<", tempreg, tempreg, 16);
		  macro_build (&offset_expr, "daddiu", "t,r,j",
			       tempreg, tempreg, BFD_RELOC_LO16);
		}
	    }
	  else
	    {
	      if ((valueT) offset_expr.X_add_number <= MAX_GPREL_OFFSET
		  && ! nopic_need_relax (offset_expr.X_add_symbol, 1))
		{
		  relax_start (offset_expr.X_add_symbol);
		  macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j",
			       tempreg, mips_gp_register, BFD_RELOC_GPREL16);
		  relax_switch ();
		}
	      macro_build_lui (&offset_expr, tempreg);
	      macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j",
			   tempreg, tempreg, BFD_RELOC_LO16);
	      if (mips_relax.sequence)
		relax_end ();
	    }
	}
      else if (mips_pic == SVR4_PIC && ! mips_big_got && ! HAVE_NEWABI)
	{
	  int lw_reloc_type = (int) BFD_RELOC_MIPS_GOT16;

	  /* If this is a reference to an external symbol, and there
	     is no constant, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	     or for lca or if tempreg is PIC_CALL_REG
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_CALL16)
	     For a local symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       nop
	       addiu	$tempreg,$tempreg,<sym>	(BFD_RELOC_LO16)

	     If we have a small constant, and this is a reference to
	     an external symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       nop
	       addiu	$tempreg,$tempreg,<constant>
	     For a local symbol, we want the same instruction
	     sequence, but we output a BFD_RELOC_LO16 reloc on the
	     addiu instruction.

	     If we have a large constant, and this is a reference to
	     an external symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       lui	$at,<hiconstant>
	       addiu	$at,$at,<loconstant>
	       addu	$tempreg,$tempreg,$at
	     For a local symbol, we want the same instruction
	     sequence, but we output a BFD_RELOC_LO16 reloc on the
	     addiu instruction.
	   */

	  if (offset_expr.X_add_number == 0)
	    {
	      if (breg == 0 && (call || tempreg == PIC_CALL_REG))
		lw_reloc_type = (int) BFD_RELOC_MIPS_CALL16;

	      relax_start (offset_expr.X_add_symbol);
	      macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
			   lw_reloc_type, mips_gp_register);
	      if (breg != 0)
		{
		  /* We're going to put in an addu instruction using
		     tempreg, so we may as well insert the nop right
		     now.  */
		  macro_build (NULL, "nop", "");
		}
	      relax_switch ();
	      macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
			   tempreg, BFD_RELOC_MIPS_GOT16, mips_gp_register);
	      macro_build (NULL, "nop", "");
	      macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j",
			   tempreg, tempreg, BFD_RELOC_LO16);
	      relax_end ();
	      /* FIXME: If breg == 0, and the next instruction uses
		 $tempreg, then if this variant case is used an extra
		 nop will be generated.  */
	    }
	  else if (offset_expr.X_add_number >= -0x8000
		   && offset_expr.X_add_number < 0x8000)
	    {
	      load_got_offset (tempreg, &offset_expr);
	      macro_build (NULL, "nop", "");
	      add_got_offset (tempreg, &offset_expr);
	    }
	  else
	    {
	      expr1.X_add_number = offset_expr.X_add_number;
	      offset_expr.X_add_number =
		((offset_expr.X_add_number + 0x8000) & 0xffff) - 0x8000;
	      load_got_offset (tempreg, &offset_expr);
	      offset_expr.X_add_number = expr1.X_add_number;
	      /* If we are going to add in a base register, and the
		 target register and the base register are the same,
		 then we are using AT as a temporary register.  Since
		 we want to load the constant into AT, we add our
		 current AT (from the global offset table) and the
		 register into the register now, and pretend we were
		 not using a base register.  */
	      if (breg == treg)
		{
		  macro_build (NULL, "nop", "");
		  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			       treg, AT, breg);
		  breg = 0;
		  tempreg = treg;
		}
	      add_got_offset_hilo (tempreg, &offset_expr, AT);
	      used_at = 1;
	    }
	}
      else if (mips_pic == SVR4_PIC && ! mips_big_got && HAVE_NEWABI)
	{
	  int add_breg_early = 0;

	  /* If this is a reference to an external, and there is no
	     constant, or local symbol (*), with or without a
	     constant, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT_DISP)
	     or for lca or if tempreg is PIC_CALL_REG
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_CALL16)

	     If we have a small constant, and this is a reference to
	     an external symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT_DISP)
	       addiu	$tempreg,$tempreg,<constant>

	     If we have a large constant, and this is a reference to
	     an external symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT_DISP)
	       lui	$at,<hiconstant>
	       addiu	$at,$at,<loconstant>
	       addu	$tempreg,$tempreg,$at

	     (*) Other assemblers seem to prefer GOT_PAGE/GOT_OFST for
	     local symbols, even though it introduces an additional
	     instruction.  */

	  if (offset_expr.X_add_number)
	    {
	      expr1.X_add_number = offset_expr.X_add_number;
	      offset_expr.X_add_number = 0;

	      relax_start (offset_expr.X_add_symbol);
	      macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
			   BFD_RELOC_MIPS_GOT_DISP, mips_gp_register);

	      if (expr1.X_add_number >= -0x8000
		  && expr1.X_add_number < 0x8000)
		{
		  macro_build (&expr1, ADDRESS_ADDI_INSN, "t,r,j",
			       tempreg, tempreg, BFD_RELOC_LO16);
		}
	      else if (IS_SEXT_32BIT_NUM (expr1.X_add_number + 0x8000))
		{
		  int dreg;

		  /* If we are going to add in a base register, and the
		     target register and the base register are the same,
		     then we are using AT as a temporary register.  Since
		     we want to load the constant into AT, we add our
		     current AT (from the global offset table) and the
		     register into the register now, and pretend we were
		     not using a base register.  */
		  if (breg != treg)
		    dreg = tempreg;
		  else
		    {
		      assert (tempreg == AT);
		      macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
				   treg, AT, breg);
		      dreg = treg;
		      add_breg_early = 1;
		    }

		  load_register (AT, &expr1, HAVE_64BIT_ADDRESSES);
		  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			       dreg, dreg, AT);

		  used_at = 1;
		}
	      else
		as_bad (_("PIC code offset overflow (max 32 signed bits)"));

	      relax_switch ();
	      offset_expr.X_add_number = expr1.X_add_number;

	      macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
			   BFD_RELOC_MIPS_GOT_DISP, mips_gp_register);
	      if (add_breg_early)
		{
		  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			       treg, tempreg, breg);
		  breg = 0;
		  tempreg = treg;
		}
	      relax_end ();
	    }
	  else if (breg == 0 && (call || tempreg == PIC_CALL_REG))
	    {
	      relax_start (offset_expr.X_add_symbol);
	      macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
			   BFD_RELOC_MIPS_CALL16, mips_gp_register);
	      relax_switch ();
	      macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
			   BFD_RELOC_MIPS_GOT_DISP, mips_gp_register);
	      relax_end ();
	    }
	  else
	    {
	      macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
			   BFD_RELOC_MIPS_GOT_DISP, mips_gp_register);
	    }
	}
      else if (mips_pic == SVR4_PIC && ! HAVE_NEWABI)
	{
	  int gpdelay;
	  int lui_reloc_type = (int) BFD_RELOC_MIPS_GOT_HI16;
	  int lw_reloc_type = (int) BFD_RELOC_MIPS_GOT_LO16;
	  int local_reloc_type = (int) BFD_RELOC_MIPS_GOT16;

	  /* This is the large GOT case.  If this is a reference to an
	     external symbol, and there is no constant, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_GOT_HI16)
	       addu	$tempreg,$tempreg,$gp
	       lw	$tempreg,<sym>($tempreg) (BFD_RELOC_MIPS_GOT_LO16)
	     or for lca or if tempreg is PIC_CALL_REG
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_CALL_HI16)
	       addu	$tempreg,$tempreg,$gp
	       lw	$tempreg,<sym>($tempreg) (BFD_RELOC_MIPS_CALL_LO16)
	     For a local symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       nop
	       addiu	$tempreg,$tempreg,<sym>	(BFD_RELOC_LO16)

	     If we have a small constant, and this is a reference to
	     an external symbol, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_GOT_HI16)
	       addu	$tempreg,$tempreg,$gp
	       lw	$tempreg,<sym>($tempreg) (BFD_RELOC_MIPS_GOT_LO16)
	       nop
	       addiu	$tempreg,$tempreg,<constant>
	     For a local symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       nop
	       addiu	$tempreg,$tempreg,<constant> (BFD_RELOC_LO16)

	     If we have a large constant, and this is a reference to
	     an external symbol, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_GOT_HI16)
	       addu	$tempreg,$tempreg,$gp
	       lw	$tempreg,<sym>($tempreg) (BFD_RELOC_MIPS_GOT_LO16)
	       lui	$at,<hiconstant>
	       addiu	$at,$at,<loconstant>
	       addu	$tempreg,$tempreg,$at
	     For a local symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       lui	$at,<hiconstant>
	       addiu	$at,$at,<loconstant>	(BFD_RELOC_LO16)
	       addu	$tempreg,$tempreg,$at
	  */

	  expr1.X_add_number = offset_expr.X_add_number;
	  offset_expr.X_add_number = 0;
	  relax_start (offset_expr.X_add_symbol);
	  gpdelay = reg_needs_delay (mips_gp_register);
	  if (expr1.X_add_number == 0 && breg == 0
	      && (call || tempreg == PIC_CALL_REG))
	    {
	      lui_reloc_type = (int) BFD_RELOC_MIPS_CALL_HI16;
	      lw_reloc_type = (int) BFD_RELOC_MIPS_CALL_LO16;
	    }
	  macro_build (&offset_expr, "lui", "t,u", tempreg, lui_reloc_type);
	  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
		       tempreg, tempreg, mips_gp_register);
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
		       tempreg, lw_reloc_type, tempreg);
	  if (expr1.X_add_number == 0)
	    {
	      if (breg != 0)
		{
		  /* We're going to put in an addu instruction using
		     tempreg, so we may as well insert the nop right
		     now.  */
		  macro_build (NULL, "nop", "");
		}
	    }
	  else if (expr1.X_add_number >= -0x8000
		   && expr1.X_add_number < 0x8000)
	    {
	      macro_build (NULL, "nop", "");
	      macro_build (&expr1, ADDRESS_ADDI_INSN, "t,r,j",
			   tempreg, tempreg, BFD_RELOC_LO16);
	    }
	  else
	    {
	      int dreg;

	      /* If we are going to add in a base register, and the
		 target register and the base register are the same,
		 then we are using AT as a temporary register.  Since
		 we want to load the constant into AT, we add our
		 current AT (from the global offset table) and the
		 register into the register now, and pretend we were
		 not using a base register.  */
	      if (breg != treg)
		dreg = tempreg;
	      else
		{
		  assert (tempreg == AT);
		  macro_build (NULL, "nop", "");
		  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			       treg, AT, breg);
		  dreg = treg;
		}

	      load_register (AT, &expr1, HAVE_64BIT_ADDRESSES);
	      macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", dreg, dreg, AT);

	      used_at = 1;
	    }
	  offset_expr.X_add_number =
	    ((expr1.X_add_number + 0x8000) & 0xffff) - 0x8000;
	  relax_switch ();

	  if (gpdelay)
	    {
	      /* This is needed because this instruction uses $gp, but
		 the first instruction on the main stream does not.  */
	      macro_build (NULL, "nop", "");
	    }

	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
		       local_reloc_type, mips_gp_register);
	  if (expr1.X_add_number >= -0x8000
	      && expr1.X_add_number < 0x8000)
	    {
	      macro_build (NULL, "nop", "");
	      macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j",
			   tempreg, tempreg, BFD_RELOC_LO16);
	      /* FIXME: If add_number is 0, and there was no base
		 register, the external symbol case ended with a load,
		 so if the symbol turns out to not be external, and
		 the next instruction uses tempreg, an unnecessary nop
		 will be inserted.  */
	    }
	  else
	    {
	      if (breg == treg)
		{
		  /* We must add in the base register now, as in the
		     external symbol case.  */
		  assert (tempreg == AT);
		  macro_build (NULL, "nop", "");
		  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			       treg, AT, breg);
		  tempreg = treg;
		  /* We set breg to 0 because we have arranged to add
		     it in in both cases.  */
		  breg = 0;
		}

	      macro_build_lui (&expr1, AT);
	      macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j",
			   AT, AT, BFD_RELOC_LO16);
	      macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			   tempreg, tempreg, AT);
	    }
	  relax_end ();
	}
      else if (mips_pic == SVR4_PIC && HAVE_NEWABI)
	{
	  int lui_reloc_type = (int) BFD_RELOC_MIPS_GOT_HI16;
	  int lw_reloc_type = (int) BFD_RELOC_MIPS_GOT_LO16;
	  int add_breg_early = 0;

	  /* This is the large GOT case.  If this is a reference to an
	     external symbol, and there is no constant, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_GOT_HI16)
	       add	$tempreg,$tempreg,$gp
	       lw	$tempreg,<sym>($tempreg) (BFD_RELOC_MIPS_GOT_LO16)
	     or for lca or if tempreg is PIC_CALL_REG
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_CALL_HI16)
	       add	$tempreg,$tempreg,$gp
	       lw	$tempreg,<sym>($tempreg) (BFD_RELOC_MIPS_CALL_LO16)

	     If we have a small constant, and this is a reference to
	     an external symbol, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_GOT_HI16)
	       add	$tempreg,$tempreg,$gp
	       lw	$tempreg,<sym>($tempreg) (BFD_RELOC_MIPS_GOT_LO16)
	       addi	$tempreg,$tempreg,<constant>

	     If we have a large constant, and this is a reference to
	     an external symbol, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_GOT_HI16)
	       addu	$tempreg,$tempreg,$gp
	       lw	$tempreg,<sym>($tempreg) (BFD_RELOC_MIPS_GOT_LO16)
	       lui	$at,<hiconstant>
	       addi	$at,$at,<loconstant>
	       add	$tempreg,$tempreg,$at

	     If we have NewABI, and we know it's a local symbol, we want
	       lw	$reg,<sym>($gp)		(BFD_RELOC_MIPS_GOT_PAGE)
	       addiu	$reg,$reg,<sym>		(BFD_RELOC_MIPS_GOT_OFST)
	     otherwise we have to resort to GOT_HI16/GOT_LO16.  */

	  relax_start (offset_expr.X_add_symbol);

	  expr1.X_add_number = offset_expr.X_add_number;
	  offset_expr.X_add_number = 0;

	  if (expr1.X_add_number == 0 && breg == 0
	      && (call || tempreg == PIC_CALL_REG))
	    {
	      lui_reloc_type = (int) BFD_RELOC_MIPS_CALL_HI16;
	      lw_reloc_type = (int) BFD_RELOC_MIPS_CALL_LO16;
	    }
	  macro_build (&offset_expr, "lui", "t,u", tempreg, lui_reloc_type);
	  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
		       tempreg, tempreg, mips_gp_register);
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
		       tempreg, lw_reloc_type, tempreg);

	  if (expr1.X_add_number == 0)
	    ;
	  else if (expr1.X_add_number >= -0x8000
		   && expr1.X_add_number < 0x8000)
	    {
	      macro_build (&expr1, ADDRESS_ADDI_INSN, "t,r,j",
			   tempreg, tempreg, BFD_RELOC_LO16);
	    }
	  else if (IS_SEXT_32BIT_NUM (expr1.X_add_number + 0x8000))
	    {
	      int dreg;

	      /* If we are going to add in a base register, and the
		 target register and the base register are the same,
		 then we are using AT as a temporary register.  Since
		 we want to load the constant into AT, we add our
		 current AT (from the global offset table) and the
		 register into the register now, and pretend we were
		 not using a base register.  */
	      if (breg != treg)
		dreg = tempreg;
	      else
		{
		  assert (tempreg == AT);
		  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			       treg, AT, breg);
		  dreg = treg;
		  add_breg_early = 1;
		}

	      load_register (AT, &expr1, HAVE_64BIT_ADDRESSES);
	      macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", dreg, dreg, AT);

	      used_at = 1;
	    }
	  else
	    as_bad (_("PIC code offset overflow (max 32 signed bits)"));

	  relax_switch ();
	  offset_expr.X_add_number = expr1.X_add_number;
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
		       BFD_RELOC_MIPS_GOT_PAGE, mips_gp_register);
	  macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j", tempreg,
		       tempreg, BFD_RELOC_MIPS_GOT_OFST);
	  if (add_breg_early)
	    {
	      macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			   treg, tempreg, breg);
	      breg = 0;
	      tempreg = treg;
	    }
	  relax_end ();
	}
      else if (mips_pic == EMBEDDED_PIC)
	{
	  /* We use
	       addiu	$tempreg,$gp,<sym>	(BFD_RELOC_GPREL16)
	     */
	  macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j", tempreg,
		       mips_gp_register, BFD_RELOC_GPREL16);
	}
      else
	abort ();

      if (breg != 0)
	{
	  char *s;

	  if (mips_pic == EMBEDDED_PIC || mips_pic == NO_PIC)
	    s = (dbl || HAVE_64BIT_ADDRESSES) ? "daddu" : "addu";
	  else
	    s = ADDRESS_ADD_INSN;

	  macro_build (NULL, s, "d,v,t", treg, tempreg, breg);
	}

      if (! used_at)
	return;

      break;

    case M_J_A:
      /* The j instruction may not be used in PIC code, since it
	 requires an absolute address.  We convert it to a b
	 instruction.  */
      if (mips_pic == NO_PIC)
	macro_build (&offset_expr, "j", "a");
      else
	macro_build (&offset_expr, "b", "p");
      return;

      /* The jal instructions must be handled as macros because when
	 generating PIC code they expand to multi-instruction
	 sequences.  Normally they are simple instructions.  */
    case M_JAL_1:
      dreg = RA;
      /* Fall through.  */
    case M_JAL_2:
      if (mips_pic == NO_PIC
	  || mips_pic == EMBEDDED_PIC)
	macro_build (NULL, "jalr", "d,s", dreg, sreg);
      else if (mips_pic == SVR4_PIC)
	{
	  if (sreg != PIC_CALL_REG)
	    as_warn (_("MIPS PIC call to register other than $25"));

	  macro_build (NULL, "jalr", "d,s", dreg, sreg);
	  if (! HAVE_NEWABI)
	    {
	      if (mips_cprestore_offset < 0)
		as_warn (_("No .cprestore pseudo-op used in PIC code"));
	      else
		{
		  if (! mips_frame_reg_valid)
		    {
		      as_warn (_("No .frame pseudo-op used in PIC code"));
		      /* Quiet this warning.  */
		      mips_frame_reg_valid = 1;
		    }
		  if (! mips_cprestore_valid)
		    {
		      as_warn (_("No .cprestore pseudo-op used in PIC code"));
		      /* Quiet this warning.  */
		      mips_cprestore_valid = 1;
		    }
		  expr1.X_add_number = mips_cprestore_offset;
  		  macro_build_ldst_constoffset (&expr1, ADDRESS_LOAD_INSN,
						mips_gp_register,
						mips_frame_reg,
						HAVE_64BIT_ADDRESSES);
		}
	    }
	}
      else
	abort ();

      return;

    case M_JAL_A:
      if (mips_pic == NO_PIC)
	macro_build (&offset_expr, "jal", "a");
      else if (mips_pic == SVR4_PIC)
	{
	  /* If this is a reference to an external symbol, and we are
	     using a small GOT, we want
	       lw	$25,<sym>($gp)		(BFD_RELOC_MIPS_CALL16)
	       nop
	       jalr	$ra,$25
	       nop
	       lw	$gp,cprestore($sp)
	     The cprestore value is set using the .cprestore
	     pseudo-op.  If we are using a big GOT, we want
	       lui	$25,<sym>		(BFD_RELOC_MIPS_CALL_HI16)
	       addu	$25,$25,$gp
	       lw	$25,<sym>($25)		(BFD_RELOC_MIPS_CALL_LO16)
	       nop
	       jalr	$ra,$25
	       nop
	       lw	$gp,cprestore($sp)
	     If the symbol is not external, we want
	       lw	$25,<sym>($gp)		(BFD_RELOC_MIPS_GOT16)
	       nop
	       addiu	$25,$25,<sym>		(BFD_RELOC_LO16)
	       jalr	$ra,$25
	       nop
	       lw $gp,cprestore($sp)

	     For NewABI, we use the same CALL16 or CALL_HI16/CALL_LO16
	     sequences above, minus nops, unless the symbol is local,
	     which enables us to use GOT_PAGE/GOT_OFST (big got) or
	     GOT_DISP.  */
	  if (HAVE_NEWABI)
	    {
	      if (! mips_big_got)
		{
		  relax_start (offset_expr.X_add_symbol);
		  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
			       PIC_CALL_REG, BFD_RELOC_MIPS_CALL16,
			       mips_gp_register);
		  relax_switch ();
		  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
			       PIC_CALL_REG, BFD_RELOC_MIPS_GOT_DISP,
			       mips_gp_register);
		  relax_end ();
		}
	      else
		{
		  relax_start (offset_expr.X_add_symbol);
		  macro_build (&offset_expr, "lui", "t,u", PIC_CALL_REG,
			       BFD_RELOC_MIPS_CALL_HI16);
		  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", PIC_CALL_REG,
			       PIC_CALL_REG, mips_gp_register);
		  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
			       PIC_CALL_REG, BFD_RELOC_MIPS_CALL_LO16,
			       PIC_CALL_REG);
		  relax_switch ();
		  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
			       PIC_CALL_REG, BFD_RELOC_MIPS_GOT_PAGE,
			       mips_gp_register);
		  macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j",
			       PIC_CALL_REG, PIC_CALL_REG,
			       BFD_RELOC_MIPS_GOT_OFST);
		  relax_end ();
		}

	      macro_build_jalr (&offset_expr);
	    }
	  else
	    {
	      relax_start (offset_expr.X_add_symbol);
	      if (! mips_big_got)
		{
		  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
			       PIC_CALL_REG, BFD_RELOC_MIPS_CALL16,
			       mips_gp_register);
		  macro_build (NULL, "nop", "");
		  relax_switch ();
		}
	      else
		{
		  int gpdelay;

		  gpdelay = reg_needs_delay (mips_gp_register);
		  macro_build (&offset_expr, "lui", "t,u", PIC_CALL_REG,
			       BFD_RELOC_MIPS_CALL_HI16);
		  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", PIC_CALL_REG,
			       PIC_CALL_REG, mips_gp_register);
		  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
			       PIC_CALL_REG, BFD_RELOC_MIPS_CALL_LO16,
			       PIC_CALL_REG);
		  macro_build (NULL, "nop", "");
		  relax_switch ();
		  if (gpdelay)
		    macro_build (NULL, "nop", "");
		}
	      macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
			   PIC_CALL_REG, BFD_RELOC_MIPS_GOT16,
			   mips_gp_register);
	      macro_build (NULL, "nop", "");
	      macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j",
			   PIC_CALL_REG, PIC_CALL_REG, BFD_RELOC_LO16);
	      relax_end ();
	      macro_build_jalr (&offset_expr);

	      if (mips_cprestore_offset < 0)
		as_warn (_("No .cprestore pseudo-op used in PIC code"));
	      else
		{
		  if (! mips_frame_reg_valid)
		    {
		      as_warn (_("No .frame pseudo-op used in PIC code"));
		      /* Quiet this warning.  */
		      mips_frame_reg_valid = 1;
		    }
		  if (! mips_cprestore_valid)
		    {
		      as_warn (_("No .cprestore pseudo-op used in PIC code"));
		      /* Quiet this warning.  */
		      mips_cprestore_valid = 1;
		    }
		  if (mips_opts.noreorder)
		    macro_build (NULL, "nop", "");
		  expr1.X_add_number = mips_cprestore_offset;
  		  macro_build_ldst_constoffset (&expr1, ADDRESS_LOAD_INSN,
						mips_gp_register,
						mips_frame_reg,
						HAVE_64BIT_ADDRESSES);
		}
	    }
	}
      else if (mips_pic == EMBEDDED_PIC)
	{
	  macro_build (&offset_expr, "bal", "p");
	  /* The linker may expand the call to a longer sequence which
	     uses $at, so we must break rather than return.  */
	  break;
	}
      else
	abort ();

      return;

    case M_LB_AB:
      s = "lb";
      goto ld;
    case M_LBU_AB:
      s = "lbu";
      goto ld;
    case M_LH_AB:
      s = "lh";
      goto ld;
    case M_LHU_AB:
      s = "lhu";
      goto ld;
    case M_LW_AB:
      s = "lw";
      goto ld;
    case M_LWC0_AB:
      s = "lwc0";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto ld;
    case M_LWC1_AB:
      s = "lwc1";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto ld;
    case M_LWC2_AB:
      s = "lwc2";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto ld;
    case M_LWC3_AB:
      s = "lwc3";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto ld;
    case M_LWL_AB:
      s = "lwl";
      lr = 1;
      goto ld;
    case M_LWR_AB:
      s = "lwr";
      lr = 1;
      goto ld;
    case M_LDC1_AB:
      if (mips_opts.arch == CPU_R4650)
	{
	  as_bad (_("opcode not supported on this processor"));
	  return;
	}
      s = "ldc1";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto ld;
    case M_LDC2_AB:
      s = "ldc2";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto ld;
    case M_LDC3_AB:
      s = "ldc3";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto ld;
    case M_LDL_AB:
      s = "ldl";
      lr = 1;
      goto ld;
    case M_LDR_AB:
      s = "ldr";
      lr = 1;
      goto ld;
    case M_LL_AB:
      s = "ll";
      goto ld;
    case M_LLD_AB:
      s = "lld";
      goto ld;
    case M_LWU_AB:
      s = "lwu";
    ld:
      if (breg == treg || coproc || lr)
	{
	  tempreg = AT;
	  used_at = 1;
	}
      else
	{
	  tempreg = treg;
	  used_at = 0;
	}
      goto ld_st;
    case M_SB_AB:
      s = "sb";
      goto st;
    case M_SH_AB:
      s = "sh";
      goto st;
    case M_SW_AB:
      s = "sw";
      goto st;
    case M_SWC0_AB:
      s = "swc0";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto st;
    case M_SWC1_AB:
      s = "swc1";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto st;
    case M_SWC2_AB:
      s = "swc2";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto st;
    case M_SWC3_AB:
      s = "swc3";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto st;
    case M_SWL_AB:
      s = "swl";
      goto st;
    case M_SWR_AB:
      s = "swr";
      goto st;
    case M_SC_AB:
      s = "sc";
      goto st;
    case M_SCD_AB:
      s = "scd";
      goto st;
    case M_SDC1_AB:
      if (mips_opts.arch == CPU_R4650)
	{
	  as_bad (_("opcode not supported on this processor"));
	  return;
	}
      s = "sdc1";
      coproc = 1;
      /* Itbl support may require additional care here.  */
      goto st;
    case M_SDC2_AB:
      s = "sdc2";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto st;
    case M_SDC3_AB:
      s = "sdc3";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto st;
    case M_SDL_AB:
      s = "sdl";
      goto st;
    case M_SDR_AB:
      s = "sdr";
    st:
      tempreg = AT;
      used_at = 1;
    ld_st:
      /* Itbl support may require additional care here.  */
      if (mask == M_LWC1_AB
	  || mask == M_SWC1_AB
	  || mask == M_LDC1_AB
	  || mask == M_SDC1_AB
	  || mask == M_L_DAB
	  || mask == M_S_DAB)
	fmt = "T,o(b)";
      else if (coproc)
	fmt = "E,o(b)";
      else
	fmt = "t,o(b)";

      /* Sign-extending 32-bit constants makes their handling easier.
         The HAVE_64BIT_GPRS... part is due to the linux kernel hack
         described below.  */
      if ((! HAVE_64BIT_ADDRESSES
	   && (! HAVE_64BIT_GPRS && offset_expr.X_op == O_constant))
          && (offset_expr.X_op == O_constant)
	  && ! ((offset_expr.X_add_number & ~((bfd_vma) 0x7fffffff))
		== ~((bfd_vma) 0x7fffffff)))
	{
	  if (offset_expr.X_add_number & ~((bfd_vma) 0xffffffff))
	    as_bad (_("constant too large"));

	  offset_expr.X_add_number = (((offset_expr.X_add_number & 0xffffffff)
				       ^ 0x80000000) - 0x80000000);
	}

      /* For embedded PIC, we allow loads where the offset is calculated
         by subtracting a symbol in the current segment from an unknown
         symbol, relative to a base register, e.g.:
		<op>	$treg, <sym>-<localsym>($breg)
	 This is used by the compiler for switch statements.  */
      if (mips_pic == EMBEDDED_PIC
          && offset_expr.X_op == O_subtract
          && (symbol_constant_p (offset_expr.X_op_symbol)
              ? S_GET_SEGMENT (offset_expr.X_op_symbol) == now_seg
              : (symbol_equated_p (offset_expr.X_op_symbol)
                 && (S_GET_SEGMENT
                     (symbol_get_value_expression (offset_expr.X_op_symbol)
                      ->X_add_symbol)
                     == now_seg)))
          && breg != 0
          && (offset_expr.X_add_number == 0
              || OUTPUT_FLAVOR == bfd_target_elf_flavour))
        {
          /* For this case, we output the instructions:
                lui     $tempreg,<sym>          (BFD_RELOC_PCREL_HI16_S)
                addiu   $tempreg,$tempreg,$breg
                <op>    $treg,<sym>($tempreg)   (BFD_RELOC_PCREL_LO16)
             If the relocation would fit entirely in 16 bits, it would be
             nice to emit:
                <op>    $treg,<sym>($breg)      (BFD_RELOC_PCREL_LO16)
             instead, but that seems quite difficult.  */
          macro_build (&offset_expr, "lui", "t,u", tempreg,
		       BFD_RELOC_PCREL_HI16_S);
          macro_build (NULL,
		       ((bfd_arch_bits_per_address (stdoutput) == 32
			 || ! ISA_HAS_64BIT_REGS (mips_opts.isa))
			? "addu" : "daddu"),
		       "d,v,t", tempreg, tempreg, breg);
          macro_build (&offset_expr, s, fmt, treg,
		       BFD_RELOC_PCREL_LO16, tempreg);
          if (! used_at)
            return;
          break;
        }

      if (offset_expr.X_op != O_constant
	  && offset_expr.X_op != O_symbol)
	{
	  as_bad (_("expression too complex"));
	  offset_expr.X_op = O_constant;
	}

      /* A constant expression in PIC code can be handled just as it
	 is in non PIC code.  */
      if (mips_pic == NO_PIC
	  || offset_expr.X_op == O_constant)
	{
	  /* If this is a reference to a GP relative symbol, and there
	     is no base register, we want
	       <op>	$treg,<sym>($gp)	(BFD_RELOC_GPREL16)
	     Otherwise, if there is no base register, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_HI16_S)
	       <op>	$treg,<sym>($tempreg)	(BFD_RELOC_LO16)
	     If we have a constant, we need two instructions anyhow,
	     so we always use the latter form.

	     If we have a base register, and this is a reference to a
	     GP relative symbol, we want
	       addu	$tempreg,$breg,$gp
	       <op>	$treg,<sym>($tempreg)	(BFD_RELOC_GPREL16)
	     Otherwise we want
	       lui	$tempreg,<sym>		(BFD_RELOC_HI16_S)
	       addu	$tempreg,$tempreg,$breg
	       <op>	$treg,<sym>($tempreg)	(BFD_RELOC_LO16)
	     With a constant we always use the latter case.

	     With 64bit address space and no base register and $at usable,
	     we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHEST)
	       lui	$at,<sym>		(BFD_RELOC_HI16_S)
	       daddiu	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHER)
	       dsll32	$tempreg,0
	       daddu	$tempreg,$at
	       <op>	$treg,<sym>($tempreg)	(BFD_RELOC_LO16)
	     If we have a base register, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHEST)
	       lui	$at,<sym>		(BFD_RELOC_HI16_S)
	       daddiu	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHER)
	       daddu	$at,$breg
	       dsll32	$tempreg,0
	       daddu	$tempreg,$at
	       <op>	$treg,<sym>($tempreg)	(BFD_RELOC_LO16)

	     Without $at we can't generate the optimal path for superscalar
	     processors here since this would require two temporary registers.
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHEST)
	       daddiu	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHER)
	       dsll	$tempreg,16
	       daddiu	$tempreg,<sym>		(BFD_RELOC_HI16_S)
	       dsll	$tempreg,16
	       <op>	$treg,<sym>($tempreg)	(BFD_RELOC_LO16)
	     If we have a base register, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHEST)
	       daddiu	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHER)
	       dsll	$tempreg,16
	       daddiu	$tempreg,<sym>		(BFD_RELOC_HI16_S)
	       dsll	$tempreg,16
	       daddu	$tempreg,$tempreg,$breg
	       <op>	$treg,<sym>($tempreg)	(BFD_RELOC_LO16)

	     If we have 64-bit addresses, as an optimization, for
	     addresses which are 32-bit constants (e.g. kseg0/kseg1
	     addresses) we fall back to the 32-bit address generation
	     mechanism since it is more efficient.  Note that due to
	     the signed offset used by memory operations, the 32-bit
	     range is shifted down by 32768 here.  This code should
	     probably attempt to generate 64-bit constants more
	     efficiently in general.

	     As an extension for architectures with 64-bit registers,
	     we don't truncate 64-bit addresses given as literal
	     constants down to 32 bits, to support existing practice
	     in the mips64 Linux (the kernel), that compiles source
	     files with -mabi=64, assembling them as o32 or n32 (with
	     -Wa,-32 or -Wa,-n32).  This is not beautiful, but since
	     the whole kernel is loaded into a memory region that is
	     addressable with sign-extended 32-bit addresses, it is
	     wasteful to compute the upper 32 bits of every
	     non-literal address, that takes more space and time.
	     Some day this should probably be implemented as an
	     assembler option, such that the kernel doesn't have to
	     use such ugly hacks, even though it will still have to
	     end up converting the binary to ELF32 for a number of
	     platforms whose boot loaders don't support ELF64
	     binaries.  */
	  if ((HAVE_64BIT_ADDRESSES
	       && ! (offset_expr.X_op == O_constant
		     && IS_SEXT_32BIT_NUM (offset_expr.X_add_number + 0x8000)))
	      || (HAVE_64BIT_GPRS
		  && offset_expr.X_op == O_constant
		  && ! IS_SEXT_32BIT_NUM (offset_expr.X_add_number + 0x8000)))
	    {
	      /* ??? We don't provide a GP-relative alternative for
		 these macros.  It used not to be possible with the
		 original relaxation code, but it could be done now.  */

	      if (used_at == 0 && ! mips_opts.noat)
		{
		  macro_build (&offset_expr, "lui", "t,u", tempreg,
			       BFD_RELOC_MIPS_HIGHEST);
		  macro_build (&offset_expr, "lui", "t,u", AT,
			       BFD_RELOC_HI16_S);
		  macro_build (&offset_expr, "daddiu", "t,r,j", tempreg,
			       tempreg, BFD_RELOC_MIPS_HIGHER);
		  if (breg != 0)
		    macro_build (NULL, "daddu", "d,v,t", AT, AT, breg);
		  macro_build (NULL, "dsll32", "d,w,<", tempreg, tempreg, 0);
		  macro_build (NULL, "daddu", "d,v,t", tempreg, tempreg, AT);
		  macro_build (&offset_expr, s, fmt, treg, BFD_RELOC_LO16,
			       tempreg);
		  used_at = 1;
		}
	      else
		{
		  macro_build (&offset_expr, "lui", "t,u", tempreg,
			       BFD_RELOC_MIPS_HIGHEST);
		  macro_build (&offset_expr, "daddiu", "t,r,j", tempreg,
			       tempreg, BFD_RELOC_MIPS_HIGHER);
		  macro_build (NULL, "dsll", "d,w,<", tempreg, tempreg, 16);
		  macro_build (&offset_expr, "daddiu", "t,r,j", tempreg,
			       tempreg, BFD_RELOC_HI16_S);
		  macro_build (NULL, "dsll", "d,w,<", tempreg, tempreg, 16);
		  if (breg != 0)
		    macro_build (NULL, "daddu", "d,v,t",
				 tempreg, tempreg, breg);
		  macro_build (&offset_expr, s, fmt, treg,
			       BFD_RELOC_LO16, tempreg);
		}

	      return;
	    }

	  if (offset_expr.X_op == O_constant
	      && ! IS_SEXT_32BIT_NUM (offset_expr.X_add_number + 0x8000))
	    as_bad (_("load/store address overflow (max 32 bits)"));

	  if (breg == 0)
	    {
	      if ((valueT) offset_expr.X_add_number <= MAX_GPREL_OFFSET
		  && ! nopic_need_relax (offset_expr.X_add_symbol, 1))
		{
		  relax_start (offset_expr.X_add_symbol);
		  macro_build (&offset_expr, s, fmt, treg, BFD_RELOC_GPREL16,
			       mips_gp_register);
		  relax_switch ();
		  used_at = 0;
		}
	      macro_build_lui (&offset_expr, tempreg);
	      macro_build (&offset_expr, s, fmt, treg,
			   BFD_RELOC_LO16, tempreg);
	      if (mips_relax.sequence)
		relax_end ();
	    }
	  else
	    {
	      if ((valueT) offset_expr.X_add_number <= MAX_GPREL_OFFSET
		  && ! nopic_need_relax (offset_expr.X_add_symbol, 1))
		{
		  relax_start (offset_expr.X_add_symbol);
		  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			       tempreg, breg, mips_gp_register);
		  macro_build (&offset_expr, s, fmt, treg,
			       BFD_RELOC_GPREL16, tempreg);
		  relax_switch ();
		}
	      macro_build_lui (&offset_expr, tempreg);
	      macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			   tempreg, tempreg, breg);
	      macro_build (&offset_expr, s, fmt, treg,
			   BFD_RELOC_LO16, tempreg);
	      if (mips_relax.sequence)
		relax_end ();
	    }
	}
      else if (mips_pic == SVR4_PIC && ! mips_big_got)
	{
	  int lw_reloc_type = (int) BFD_RELOC_MIPS_GOT16;

	  /* If this is a reference to an external symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       nop
	       <op>	$treg,0($tempreg)
	     Otherwise we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       nop
	       addiu	$tempreg,$tempreg,<sym>	(BFD_RELOC_LO16)
	       <op>	$treg,0($tempreg)

	     For NewABI, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT_PAGE)
	       <op>	$treg,<sym>($tempreg)   (BFD_RELOC_MIPS_GOT_OFST)

	     If there is a base register, we add it to $tempreg before
	     the <op>.  If there is a constant, we stick it in the
	     <op> instruction.  We don't handle constants larger than
	     16 bits, because we have no way to load the upper 16 bits
	     (actually, we could handle them for the subset of cases
	     in which we are not using $at).  */
	  assert (offset_expr.X_op == O_symbol);
	  if (HAVE_NEWABI)
	    {
	      macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
			   BFD_RELOC_MIPS_GOT_PAGE, mips_gp_register);
	      if (breg != 0)
		macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			     tempreg, tempreg, breg);
	      macro_build (&offset_expr, s, fmt, treg,
			   BFD_RELOC_MIPS_GOT_OFST, tempreg);

	      if (! used_at)
		return;

	      break;
	    }
	  expr1.X_add_number = offset_expr.X_add_number;
	  offset_expr.X_add_number = 0;
	  if (expr1.X_add_number < -0x8000
	      || expr1.X_add_number >= 0x8000)
	    as_bad (_("PIC code offset overflow (max 16 signed bits)"));
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
		       lw_reloc_type, mips_gp_register);
	  macro_build (NULL, "nop", "");
	  relax_start (offset_expr.X_add_symbol);
	  relax_switch ();
	  macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j", tempreg,
		       tempreg, BFD_RELOC_LO16);
	  relax_end ();
	  if (breg != 0)
	    macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			 tempreg, tempreg, breg);
	  macro_build (&expr1, s, fmt, treg, BFD_RELOC_LO16, tempreg);
	}
      else if (mips_pic == SVR4_PIC && ! HAVE_NEWABI)
	{
	  int gpdelay;

	  /* If this is a reference to an external symbol, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_GOT_HI16)
	       addu	$tempreg,$tempreg,$gp
	       lw	$tempreg,<sym>($tempreg) (BFD_RELOC_MIPS_GOT_LO16)
	       <op>	$treg,0($tempreg)
	     Otherwise we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       nop
	       addiu	$tempreg,$tempreg,<sym>	(BFD_RELOC_LO16)
	       <op>	$treg,0($tempreg)
	     If there is a base register, we add it to $tempreg before
	     the <op>.  If there is a constant, we stick it in the
	     <op> instruction.  We don't handle constants larger than
	     16 bits, because we have no way to load the upper 16 bits
	     (actually, we could handle them for the subset of cases
	     in which we are not using $at).  */
	  assert (offset_expr.X_op == O_symbol);
	  expr1.X_add_number = offset_expr.X_add_number;
	  offset_expr.X_add_number = 0;
	  if (expr1.X_add_number < -0x8000
	      || expr1.X_add_number >= 0x8000)
	    as_bad (_("PIC code offset overflow (max 16 signed bits)"));
	  gpdelay = reg_needs_delay (mips_gp_register);
	  relax_start (offset_expr.X_add_symbol);
	  macro_build (&offset_expr, "lui", "t,u", tempreg,
		       BFD_RELOC_MIPS_GOT_HI16);
	  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", tempreg, tempreg,
		       mips_gp_register);
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
		       BFD_RELOC_MIPS_GOT_LO16, tempreg);
	  relax_switch ();
	  if (gpdelay)
	    macro_build (NULL, "nop", "");
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
		       BFD_RELOC_MIPS_GOT16, mips_gp_register);
	  macro_build (NULL, "nop", "");
	  macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j", tempreg,
		       tempreg, BFD_RELOC_LO16);
	  relax_end ();

	  if (breg != 0)
	    macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			 tempreg, tempreg, breg);
	  macro_build (&expr1, s, fmt, treg, BFD_RELOC_LO16, tempreg);
	}
      else if (mips_pic == SVR4_PIC && HAVE_NEWABI)
	{
	  /* If this is a reference to an external symbol, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_GOT_HI16)
	       add	$tempreg,$tempreg,$gp
	       lw	$tempreg,<sym>($tempreg) (BFD_RELOC_MIPS_GOT_LO16)
	       <op>	$treg,<ofst>($tempreg)
	     Otherwise, for local symbols, we want:
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT_PAGE)
	       <op>	$treg,<sym>($tempreg)   (BFD_RELOC_MIPS_GOT_OFST)  */
	  assert (offset_expr.X_op == O_symbol);
	  expr1.X_add_number = offset_expr.X_add_number;
	  offset_expr.X_add_number = 0;
	  if (expr1.X_add_number < -0x8000
	      || expr1.X_add_number >= 0x8000)
	    as_bad (_("PIC code offset overflow (max 16 signed bits)"));
	  relax_start (offset_expr.X_add_symbol);
	  macro_build (&offset_expr, "lui", "t,u", tempreg,
		       BFD_RELOC_MIPS_GOT_HI16);
	  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", tempreg, tempreg,
		       mips_gp_register);
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
		       BFD_RELOC_MIPS_GOT_LO16, tempreg);
	  if (breg != 0)
	    macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			 tempreg, tempreg, breg);
	  macro_build (&expr1, s, fmt, treg, BFD_RELOC_LO16, tempreg);

	  relax_switch ();
	  offset_expr.X_add_number = expr1.X_add_number;
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
		       BFD_RELOC_MIPS_GOT_PAGE, mips_gp_register);
	  if (breg != 0)
	    macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			 tempreg, tempreg, breg);
	  macro_build (&offset_expr, s, fmt, treg,
		       BFD_RELOC_MIPS_GOT_OFST, tempreg);
	  relax_end ();
	}
      else if (mips_pic == EMBEDDED_PIC)
	{
	  /* If there is no base register, we want
	       <op>	$treg,<sym>($gp)	(BFD_RELOC_GPREL16)
	     If there is a base register, we want
	       addu	$tempreg,$breg,$gp
	       <op>	$treg,<sym>($tempreg)	(BFD_RELOC_GPREL16)
	     */
	  assert (offset_expr.X_op == O_symbol);
	  if (breg == 0)
	    {
	      macro_build (&offset_expr, s, fmt, treg, BFD_RELOC_GPREL16,
			   mips_gp_register);
	      used_at = 0;
	    }
	  else
	    {
	      macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			   tempreg, breg, mips_gp_register);
	      macro_build (&offset_expr, s, fmt, treg,
			   BFD_RELOC_GPREL16, tempreg);
	    }
	}
      else
	abort ();

      if (! used_at)
	return;

      break;

    case M_LI:
    case M_LI_S:
      load_register (treg, &imm_expr, 0);
      return;

    case M_DLI:
      load_register (treg, &imm_expr, 1);
      return;

    case M_LI_SS:
      if (imm_expr.X_op == O_constant)
	{
	  load_register (AT, &imm_expr, 0);
	  macro_build (NULL, "mtc1", "t,G", AT, treg);
	  break;
	}
      else
	{
	  assert (offset_expr.X_op == O_symbol
		  && strcmp (segment_name (S_GET_SEGMENT
					   (offset_expr.X_add_symbol)),
			     ".lit4") == 0
		  && offset_expr.X_add_number == 0);
	  macro_build (&offset_expr, "lwc1", "T,o(b)", treg,
		       BFD_RELOC_MIPS_LITERAL, mips_gp_register);
	  return;
	}

    case M_LI_D:
      /* Check if we have a constant in IMM_EXPR.  If the GPRs are 64 bits
         wide, IMM_EXPR is the entire value.  Otherwise IMM_EXPR is the high
         order 32 bits of the value and the low order 32 bits are either
         zero or in OFFSET_EXPR.  */
      if (imm_expr.X_op == O_constant || imm_expr.X_op == O_big)
	{
	  if (HAVE_64BIT_GPRS)
	    load_register (treg, &imm_expr, 1);
	  else
	    {
	      int hreg, lreg;

	      if (target_big_endian)
		{
		  hreg = treg;
		  lreg = treg + 1;
		}
	      else
		{
		  hreg = treg + 1;
		  lreg = treg;
		}

	      if (hreg <= 31)
		load_register (hreg, &imm_expr, 0);
	      if (lreg <= 31)
		{
		  if (offset_expr.X_op == O_absent)
		    move_register (lreg, 0);
		  else
		    {
		      assert (offset_expr.X_op == O_constant);
		      load_register (lreg, &offset_expr, 0);
		    }
		}
	    }
	  return;
	}

      /* We know that sym is in the .rdata section.  First we get the
	 upper 16 bits of the address.  */
      if (mips_pic == NO_PIC)
	{
	  macro_build_lui (&offset_expr, AT);
	}
      else if (mips_pic == SVR4_PIC)
	{
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", AT,
		       BFD_RELOC_MIPS_GOT16, mips_gp_register);
	}
      else if (mips_pic == EMBEDDED_PIC)
	{
	  /* For embedded PIC we pick up the entire address off $gp in
	     a single instruction.  */
	  macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j", AT,
		       mips_gp_register, BFD_RELOC_GPREL16);
	  offset_expr.X_op = O_constant;
	  offset_expr.X_add_number = 0;
	}
      else
	abort ();

      /* Now we load the register(s).  */
      if (HAVE_64BIT_GPRS)
	macro_build (&offset_expr, "ld", "t,o(b)", treg, BFD_RELOC_LO16, AT);
      else
	{
	  macro_build (&offset_expr, "lw", "t,o(b)", treg, BFD_RELOC_LO16, AT);
	  if (treg != RA)
	    {
	      /* FIXME: How in the world do we deal with the possible
		 overflow here?  */
	      offset_expr.X_add_number += 4;
	      macro_build (&offset_expr, "lw", "t,o(b)",
			   treg + 1, BFD_RELOC_LO16, AT);
	    }
	}
      break;

    case M_LI_DD:
      /* Check if we have a constant in IMM_EXPR.  If the FPRs are 64 bits
         wide, IMM_EXPR is the entire value and the GPRs are known to be 64
         bits wide as well.  Otherwise IMM_EXPR is the high order 32 bits of
         the value and the low order 32 bits are either zero or in
         OFFSET_EXPR.  */
      if (imm_expr.X_op == O_constant || imm_expr.X_op == O_big)
	{
	  load_register (AT, &imm_expr, HAVE_64BIT_FPRS);
	  if (HAVE_64BIT_FPRS)
	    {
	      assert (HAVE_64BIT_GPRS);
	      macro_build (NULL, "dmtc1", "t,S", AT, treg);
	    }
	  else
	    {
	      macro_build (NULL, "mtc1", "t,G", AT, treg + 1);
	      if (offset_expr.X_op == O_absent)
		macro_build (NULL, "mtc1", "t,G", 0, treg);
	      else
		{
		  assert (offset_expr.X_op == O_constant);
		  load_register (AT, &offset_expr, 0);
		  macro_build (NULL, "mtc1", "t,G", AT, treg);
		}
	    }
	  break;
	}

      assert (offset_expr.X_op == O_symbol
	      && offset_expr.X_add_number == 0);
      s = segment_name (S_GET_SEGMENT (offset_expr.X_add_symbol));
      if (strcmp (s, ".lit8") == 0)
	{
	  if (mips_opts.isa != ISA_MIPS1)
	    {
	      macro_build (&offset_expr, "ldc1", "T,o(b)", treg,
			   BFD_RELOC_MIPS_LITERAL, mips_gp_register);
	      return;
	    }
	  breg = mips_gp_register;
	  r = BFD_RELOC_MIPS_LITERAL;
	  goto dob;
	}
      else
	{
	  assert (strcmp (s, RDATA_SECTION_NAME) == 0);
	  if (mips_pic == SVR4_PIC)
	    macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", AT,
			 BFD_RELOC_MIPS_GOT16, mips_gp_register);
	  else
	    {
	      /* FIXME: This won't work for a 64 bit address.  */
	      macro_build_lui (&offset_expr, AT);
	    }

	  if (mips_opts.isa != ISA_MIPS1)
	    {
	      macro_build (&offset_expr, "ldc1", "T,o(b)",
			   treg, BFD_RELOC_LO16, AT);
	      break;
	    }
	  breg = AT;
	  r = BFD_RELOC_LO16;
	  goto dob;
	}

    case M_L_DOB:
      if (mips_opts.arch == CPU_R4650)
	{
	  as_bad (_("opcode not supported on this processor"));
	  return;
	}
      /* Even on a big endian machine $fn comes before $fn+1.  We have
	 to adjust when loading from memory.  */
      r = BFD_RELOC_LO16;
    dob:
      assert (mips_opts.isa == ISA_MIPS1);
      macro_build (&offset_expr, "lwc1", "T,o(b)",
		   target_big_endian ? treg + 1 : treg, r, breg);
      /* FIXME: A possible overflow which I don't know how to deal
	 with.  */
      offset_expr.X_add_number += 4;
      macro_build (&offset_expr, "lwc1", "T,o(b)",
		   target_big_endian ? treg : treg + 1, r, breg);

      if (breg != AT)
	return;
      break;

    case M_L_DAB:
      /*
       * The MIPS assembler seems to check for X_add_number not
       * being double aligned and generating:
       *	lui	at,%hi(foo+1)
       *	addu	at,at,v1
       *	addiu	at,at,%lo(foo+1)
       *	lwc1	f2,0(at)
       *	lwc1	f3,4(at)
       * But, the resulting address is the same after relocation so why
       * generate the extra instruction?
       */
      if (mips_opts.arch == CPU_R4650)
	{
	  as_bad (_("opcode not supported on this processor"));
	  return;
	}
      /* Itbl support may require additional care here.  */
      coproc = 1;
      if (mips_opts.isa != ISA_MIPS1)
	{
	  s = "ldc1";
	  goto ld;
	}

      s = "lwc1";
      fmt = "T,o(b)";
      goto ldd_std;

    case M_S_DAB:
      if (mips_opts.arch == CPU_R4650)
	{
	  as_bad (_("opcode not supported on this processor"));
	  return;
	}

      if (mips_opts.isa != ISA_MIPS1)
	{
	  s = "sdc1";
	  goto st;
	}

      s = "swc1";
      fmt = "T,o(b)";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto ldd_std;

    case M_LD_AB:
      if (HAVE_64BIT_GPRS)
	{
	  s = "ld";
	  goto ld;
	}

      s = "lw";
      fmt = "t,o(b)";
      goto ldd_std;

    case M_SD_AB:
      if (HAVE_64BIT_GPRS)
	{
	  s = "sd";
	  goto st;
	}

      s = "sw";
      fmt = "t,o(b)";

    ldd_std:
      /* We do _not_ bother to allow embedded PIC (symbol-local_symbol)
	 loads for the case of doing a pair of loads to simulate an 'ld'.
	 This is not currently done by the compiler, and assembly coders
	 writing embedded-pic code can cope.  */

      if (offset_expr.X_op != O_symbol
	  && offset_expr.X_op != O_constant)
	{
	  as_bad (_("expression too complex"));
	  offset_expr.X_op = O_constant;
	}

      /* Even on a big endian machine $fn comes before $fn+1.  We have
	 to adjust when loading from memory.  We set coproc if we must
	 load $fn+1 first.  */
      /* Itbl support may require additional care here.  */
      if (! target_big_endian)
	coproc = 0;

      if (mips_pic == NO_PIC
	  || offset_expr.X_op == O_constant)
	{
	  /* If this is a reference to a GP relative symbol, we want
	       <op>	$treg,<sym>($gp)	(BFD_RELOC_GPREL16)
	       <op>	$treg+1,<sym>+4($gp)	(BFD_RELOC_GPREL16)
	     If we have a base register, we use this
	       addu	$at,$breg,$gp
	       <op>	$treg,<sym>($at)	(BFD_RELOC_GPREL16)
	       <op>	$treg+1,<sym>+4($at)	(BFD_RELOC_GPREL16)
	     If this is not a GP relative symbol, we want
	       lui	$at,<sym>		(BFD_RELOC_HI16_S)
	       <op>	$treg,<sym>($at)	(BFD_RELOC_LO16)
	       <op>	$treg+1,<sym>+4($at)	(BFD_RELOC_LO16)
	     If there is a base register, we add it to $at after the
	     lui instruction.  If there is a constant, we always use
	     the last case.  */
	  if ((valueT) offset_expr.X_add_number > MAX_GPREL_OFFSET
	      || nopic_need_relax (offset_expr.X_add_symbol, 1))
	    used_at = 1;
	  else
	    {
	      relax_start (offset_expr.X_add_symbol);
	      if (breg == 0)
		{
		  tempreg = mips_gp_register;
		  used_at = 0;
		}
	      else
		{
		  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			       AT, breg, mips_gp_register);
		  tempreg = AT;
		  used_at = 1;
		}

	      /* Itbl support may require additional care here.  */
	      macro_build (&offset_expr, s, fmt, coproc ? treg + 1 : treg,
			   BFD_RELOC_GPREL16, tempreg);
	      offset_expr.X_add_number += 4;

	      /* Set mips_optimize to 2 to avoid inserting an
                 undesired nop.  */
	      hold_mips_optimize = mips_optimize;
	      mips_optimize = 2;
	      /* Itbl support may require additional care here.  */
	      macro_build (&offset_expr, s, fmt, coproc ? treg : treg + 1,
			   BFD_RELOC_GPREL16, tempreg);
	      mips_optimize = hold_mips_optimize;

	      relax_switch ();

	      /* We just generated two relocs.  When tc_gen_reloc
		 handles this case, it will skip the first reloc and
		 handle the second.  The second reloc already has an
		 extra addend of 4, which we added above.  We must
		 subtract it out, and then subtract another 4 to make
		 the first reloc come out right.  The second reloc
		 will come out right because we are going to add 4 to
		 offset_expr when we build its instruction below.

		 If we have a symbol, then we don't want to include
		 the offset, because it will wind up being included
		 when we generate the reloc.  */

	      if (offset_expr.X_op == O_constant)
		offset_expr.X_add_number -= 8;
	      else
		{
		  offset_expr.X_add_number = -4;
		  offset_expr.X_op = O_constant;
		}
	    }
	  macro_build_lui (&offset_expr, AT);
	  if (breg != 0)
	    macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", AT, breg, AT);
	  /* Itbl support may require additional care here.  */
	  macro_build (&offset_expr, s, fmt, coproc ? treg + 1 : treg,
		       BFD_RELOC_LO16, AT);
	  /* FIXME: How do we handle overflow here?  */
	  offset_expr.X_add_number += 4;
	  /* Itbl support may require additional care here.  */
	  macro_build (&offset_expr, s, fmt, coproc ? treg : treg + 1,
		       BFD_RELOC_LO16, AT);
	  if (mips_relax.sequence)
	    relax_end ();
	}
      else if (mips_pic == SVR4_PIC && ! mips_big_got)
	{
	  /* If this is a reference to an external symbol, we want
	       lw	$at,<sym>($gp)		(BFD_RELOC_MIPS_GOT16)
	       nop
	       <op>	$treg,0($at)
	       <op>	$treg+1,4($at)
	     Otherwise we want
	       lw	$at,<sym>($gp)		(BFD_RELOC_MIPS_GOT16)
	       nop
	       <op>	$treg,<sym>($at)	(BFD_RELOC_LO16)
	       <op>	$treg+1,<sym>+4($at)	(BFD_RELOC_LO16)
	     If there is a base register we add it to $at before the
	     lwc1 instructions.  If there is a constant we include it
	     in the lwc1 instructions.  */
	  used_at = 1;
	  expr1.X_add_number = offset_expr.X_add_number;
	  if (expr1.X_add_number < -0x8000
	      || expr1.X_add_number >= 0x8000 - 4)
	    as_bad (_("PIC code offset overflow (max 16 signed bits)"));
	  load_got_offset (AT, &offset_expr);
	  macro_build (NULL, "nop", "");
	  if (breg != 0)
	    macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", AT, breg, AT);

	  /* Set mips_optimize to 2 to avoid inserting an undesired
             nop.  */
	  hold_mips_optimize = mips_optimize;
	  mips_optimize = 2;

	  /* Itbl support may require additional care here.  */
	  relax_start (offset_expr.X_add_symbol);
	  macro_build (&expr1, s, fmt, coproc ? treg + 1 : treg,
		       BFD_RELOC_LO16, AT);
	  expr1.X_add_number += 4;
	  macro_build (&expr1, s, fmt, coproc ? treg : treg + 1,
		       BFD_RELOC_LO16, AT);
	  relax_switch ();
	  macro_build (&offset_expr, s, fmt, coproc ? treg + 1 : treg,
		       BFD_RELOC_LO16, AT);
	  offset_expr.X_add_number += 4;
	  macro_build (&offset_expr, s, fmt, coproc ? treg : treg + 1,
		       BFD_RELOC_LO16, AT);
	  relax_end ();

	  mips_optimize = hold_mips_optimize;
	}
      else if (mips_pic == SVR4_PIC)
	{
	  int gpdelay;

	  /* If this is a reference to an external symbol, we want
	       lui	$at,<sym>		(BFD_RELOC_MIPS_GOT_HI16)
	       addu	$at,$at,$gp
	       lw	$at,<sym>($at)		(BFD_RELOC_MIPS_GOT_LO16)
	       nop
	       <op>	$treg,0($at)
	       <op>	$treg+1,4($at)
	     Otherwise we want
	       lw	$at,<sym>($gp)		(BFD_RELOC_MIPS_GOT16)
	       nop
	       <op>	$treg,<sym>($at)	(BFD_RELOC_LO16)
	       <op>	$treg+1,<sym>+4($at)	(BFD_RELOC_LO16)
	     If there is a base register we add it to $at before the
	     lwc1 instructions.  If there is a constant we include it
	     in the lwc1 instructions.  */
	  used_at = 1;
	  expr1.X_add_number = offset_expr.X_add_number;
	  offset_expr.X_add_number = 0;
	  if (expr1.X_add_number < -0x8000
	      || expr1.X_add_number >= 0x8000 - 4)
	    as_bad (_("PIC code offset overflow (max 16 signed bits)"));
	  gpdelay = reg_needs_delay (mips_gp_register);
	  relax_start (offset_expr.X_add_symbol);
	  macro_build (&offset_expr, "lui", "t,u",
		       AT, BFD_RELOC_MIPS_GOT_HI16);
	  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
		       AT, AT, mips_gp_register);
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
		       AT, BFD_RELOC_MIPS_GOT_LO16, AT);
	  macro_build (NULL, "nop", "");
	  if (breg != 0)
	    macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", AT, breg, AT);
	  /* Itbl support may require additional care here.  */
	  macro_build (&expr1, s, fmt, coproc ? treg + 1 : treg,
		       BFD_RELOC_LO16, AT);
	  expr1.X_add_number += 4;

	  /* Set mips_optimize to 2 to avoid inserting an undesired
             nop.  */
	  hold_mips_optimize = mips_optimize;
	  mips_optimize = 2;
	  /* Itbl support may require additional care here.  */
	  macro_build (&expr1, s, fmt, coproc ? treg : treg + 1,
		       BFD_RELOC_LO16, AT);
	  mips_optimize = hold_mips_optimize;
	  expr1.X_add_number -= 4;

	  relax_switch ();
	  offset_expr.X_add_number = expr1.X_add_number;
	  if (gpdelay)
	    macro_build (NULL, "nop", "");
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", AT,
		       BFD_RELOC_MIPS_GOT16, mips_gp_register);
	  macro_build (NULL, "nop", "");
	  if (breg != 0)
	    macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", AT, breg, AT);
	  /* Itbl support may require additional care here.  */
	  macro_build (&offset_expr, s, fmt, coproc ? treg + 1 : treg,
		       BFD_RELOC_LO16, AT);
	  offset_expr.X_add_number += 4;

	  /* Set mips_optimize to 2 to avoid inserting an undesired
             nop.  */
	  hold_mips_optimize = mips_optimize;
	  mips_optimize = 2;
	  /* Itbl support may require additional care here.  */
	  macro_build (&offset_expr, s, fmt, coproc ? treg : treg + 1,
		       BFD_RELOC_LO16, AT);
	  mips_optimize = hold_mips_optimize;
	  relax_end ();
	}
      else if (mips_pic == EMBEDDED_PIC)
	{
	  /* If there is no base register, we use
	       <op>	$treg,<sym>($gp)	(BFD_RELOC_GPREL16)
	       <op>	$treg+1,<sym>+4($gp)	(BFD_RELOC_GPREL16)
	     If we have a base register, we use
	       addu	$at,$breg,$gp
	       <op>	$treg,<sym>($at)	(BFD_RELOC_GPREL16)
	       <op>	$treg+1,<sym>+4($at)	(BFD_RELOC_GPREL16)
	     */
	  if (breg == 0)
	    {
	      tempreg = mips_gp_register;
	      used_at = 0;
	    }
	  else
	    {
	      macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			   AT, breg, mips_gp_register);
	      tempreg = AT;
	      used_at = 1;
	    }

	  /* Itbl support may require additional care here.  */
	  macro_build (&offset_expr, s, fmt, coproc ? treg + 1 : treg,
		       BFD_RELOC_GPREL16, tempreg);
	  offset_expr.X_add_number += 4;
	  /* Itbl support may require additional care here.  */
	  macro_build (&offset_expr, s, fmt, coproc ? treg : treg + 1,
		       BFD_RELOC_GPREL16, tempreg);
	}
      else
	abort ();

      if (! used_at)
	return;

      break;

    case M_LD_OB:
      s = "lw";
      goto sd_ob;
    case M_SD_OB:
      s = "sw";
    sd_ob:
      assert (HAVE_32BIT_ADDRESSES);
      macro_build (&offset_expr, s, "t,o(b)", treg, BFD_RELOC_LO16, breg);
      offset_expr.X_add_number += 4;
      macro_build (&offset_expr, s, "t,o(b)", treg + 1, BFD_RELOC_LO16, breg);
      return;

   /* New code added to support COPZ instructions.
      This code builds table entries out of the macros in mip_opcodes.
      R4000 uses interlocks to handle coproc delays.
      Other chips (like the R3000) require nops to be inserted for delays.

      FIXME: Currently, we require that the user handle delays.
      In order to fill delay slots for non-interlocked chips,
      we must have a way to specify delays based on the coprocessor.
      Eg. 4 cycles if load coproc reg from memory, 1 if in cache, etc.
      What are the side-effects of the cop instruction?
      What cache support might we have and what are its effects?
      Both coprocessor & memory require delays. how long???
      What registers are read/set/modified?

      If an itbl is provided to interpret cop instructions,
      this knowledge can be encoded in the itbl spec.  */

    case M_COP0:
      s = "c0";
      goto copz;
    case M_COP1:
      s = "c1";
      goto copz;
    case M_COP2:
      s = "c2";
      goto copz;
    case M_COP3:
      s = "c3";
    copz:
      /* For now we just do C (same as Cz).  The parameter will be
         stored in insn_opcode by mips_ip.  */
      macro_build (NULL, s, "C", ip->insn_opcode);
      return;

    case M_MOVE:
      move_register (dreg, sreg);
      return;

#ifdef LOSING_COMPILER
    default:
      /* Try and see if this is a new itbl instruction.
         This code builds table entries out of the macros in mip_opcodes.
         FIXME: For now we just assemble the expression and pass it's
         value along as a 32-bit immediate.
         We may want to have the assembler assemble this value,
         so that we gain the assembler's knowledge of delay slots,
         symbols, etc.
         Would it be more efficient to use mask (id) here? */
      if (itbl_have_entries
	  && (immed_expr = itbl_assemble (ip->insn_mo->name, "")))
	{
	  s = ip->insn_mo->name;
	  s2 = "cop3";
	  coproc = ITBL_DECODE_PNUM (immed_expr);;
	  macro_build (&immed_expr, s, "C");
	  return;
	}
      macro2 (ip);
      return;
    }
  if (mips_opts.noat)
    as_warn (_("Macro used $at after \".set noat\""));
}

static void
macro2 (struct mips_cl_insn *ip)
{
  register int treg, sreg, dreg, breg;
  int tempreg;
  int mask;
  int used_at;
  expressionS expr1;
  const char *s;
  const char *s2;
  const char *fmt;
  int likely = 0;
  int dbl = 0;
  int coproc = 0;
  int lr = 0;
  int imm = 0;
  int off;
  offsetT maxnum;
  bfd_reloc_code_real_type r;

  treg = (ip->insn_opcode >> 16) & 0x1f;
  dreg = (ip->insn_opcode >> 11) & 0x1f;
  sreg = breg = (ip->insn_opcode >> 21) & 0x1f;
  mask = ip->insn_mo->mask;

  expr1.X_op = O_constant;
  expr1.X_op_symbol = NULL;
  expr1.X_add_symbol = NULL;
  expr1.X_add_number = 1;

  switch (mask)
    {
#endif /* LOSING_COMPILER */

    case M_DMUL:
      dbl = 1;
    case M_MUL:
      macro_build (NULL, dbl ? "dmultu" : "multu", "s,t", sreg, treg);
      macro_build (NULL, "mflo", "d", dreg);
      return;

    case M_DMUL_I:
      dbl = 1;
    case M_MUL_I:
      /* The MIPS assembler some times generates shifts and adds.  I'm
	 not trying to be that fancy. GCC should do this for us
	 anyway.  */
      load_register (AT, &imm_expr, dbl);
      macro_build (NULL, dbl ? "dmult" : "mult", "s,t", sreg, AT);
      macro_build (NULL, "mflo", "d", dreg);
      break;

    case M_DMULO_I:
      dbl = 1;
    case M_MULO_I:
      imm = 1;
      goto do_mulo;

    case M_DMULO:
      dbl = 1;
    case M_MULO:
    do_mulo:
      mips_emit_delays (TRUE);
      ++mips_opts.noreorder;
      mips_any_noreorder = 1;
      if (imm)
	load_register (AT, &imm_expr, dbl);
      macro_build (NULL, dbl ? "dmult" : "mult", "s,t", sreg, imm ? AT : treg);
      macro_build (NULL, "mflo", "d", dreg);
      macro_build (NULL, dbl ? "dsra32" : "sra", "d,w,<", dreg, dreg, RA);
      macro_build (NULL, "mfhi", "d", AT);
      if (mips_trap)
	macro_build (NULL, "tne", "s,t,q", dreg, AT, 6);
      else
	{
	  expr1.X_add_number = 8;
	  macro_build (&expr1, "beq", "s,t,p", dreg, AT);
	  macro_build (NULL, "nop", "", 0);
	  macro_build (NULL, "break", "c", 6);
	}
      --mips_opts.noreorder;
      macro_build (NULL, "mflo", "d", dreg);
      break;

    case M_DMULOU_I:
      dbl = 1;
    case M_MULOU_I:
      imm = 1;
      goto do_mulou;

    case M_DMULOU:
      dbl = 1;
    case M_MULOU:
    do_mulou:
      mips_emit_delays (TRUE);
      ++mips_opts.noreorder;
      mips_any_noreorder = 1;
      if (imm)
	load_register (AT, &imm_expr, dbl);
      macro_build (NULL, dbl ? "dmultu" : "multu", "s,t",
		   sreg, imm ? AT : treg);
      macro_build (NULL, "mfhi", "d", AT);
      macro_build (NULL, "mflo", "d", dreg);
      if (mips_trap)
	macro_build (NULL, "tne", "s,t,q", AT, 0, 6);
      else
	{
	  expr1.X_add_number = 8;
	  macro_build (&expr1, "beq", "s,t,p", AT, 0);
	  macro_build (NULL, "nop", "", 0);
	  macro_build (NULL, "break", "c", 6);
	}
      --mips_opts.noreorder;
      break;

    case M_DROL:
      if (ISA_HAS_DROR (mips_opts.isa) || CPU_HAS_DROR (mips_opts.arch))
	{
	  if (dreg == sreg)
	    {
	      tempreg = AT;
	      used_at = 1;
	    }
	  else
	    {
	      tempreg = dreg;
	      used_at = 0;
	    }
	  macro_build (NULL, "dnegu", "d,w", tempreg, treg);
	  macro_build (NULL, "drorv", "d,t,s", dreg, sreg, tempreg);
	  if (used_at)
	    break;
	  return;
	}
      macro_build (NULL, "dsubu", "d,v,t", AT, 0, treg);
      macro_build (NULL, "dsrlv", "d,t,s", AT, sreg, AT);
      macro_build (NULL, "dsllv", "d,t,s", dreg, sreg, treg);
      macro_build (NULL, "or", "d,v,t", dreg, dreg, AT);
      break;

    case M_ROL:
      if (ISA_HAS_ROR (mips_opts.isa) || CPU_HAS_ROR (mips_opts.arch))
	{
	  if (dreg == sreg)
	    {
	      tempreg = AT;
	      used_at = 1;
	    }
	  else
	    {
	      tempreg = dreg;
	      used_at = 0;
	    }
	  macro_build (NULL, "negu", "d,w", tempreg, treg);
	  macro_build (NULL, "rorv", "d,t,s", dreg, sreg, tempreg);
	  if (used_at)
	    break;
	  return;
	}
      macro_build (NULL, "subu", "d,v,t", AT, 0, treg);
      macro_build (NULL, "srlv", "d,t,s", AT, sreg, AT);
      macro_build (NULL, "sllv", "d,t,s", dreg, sreg, treg);
      macro_build (NULL, "or", "d,v,t", dreg, dreg, AT);
      break;

    case M_DROL_I:
      {
	unsigned int rot;
	char *l, *r;

	if (imm_expr.X_op != O_constant)
	  as_bad (_("Improper rotate count"));
	rot = imm_expr.X_add_number & 0x3f;
	if (ISA_HAS_DROR (mips_opts.isa) || CPU_HAS_DROR (mips_opts.arch))
	  {
	    rot = (64 - rot) & 0x3f;
	    if (rot >= 32)
	      macro_build (NULL, "dror32", "d,w,<", dreg, sreg, rot - 32);
	    else
	      macro_build (NULL, "dror", "d,w,<", dreg, sreg, rot);
	    return;
	  }
	if (rot == 0)
	  {
	    macro_build (NULL, "dsrl", "d,w,<", dreg, sreg, 0);
	    return;
	  }
	l = (rot < 0x20) ? "dsll" : "dsll32";
	r = ((0x40 - rot) < 0x20) ? "dsrl" : "dsrl32";
	rot &= 0x1f;
	macro_build (NULL, l, "d,w,<", AT, sreg, rot);
	macro_build (NULL, r, "d,w,<", dreg, sreg, (0x20 - rot) & 0x1f);
	macro_build (NULL, "or", "d,v,t", dreg, dreg, AT);
      }
      break;

    case M_ROL_I:
      {
	unsigned int rot;

	if (imm_expr.X_op != O_constant)
	  as_bad (_("Improper rotate count"));
	rot = imm_expr.X_add_number & 0x1f;
	if (ISA_HAS_ROR (mips_opts.isa) || CPU_HAS_ROR (mips_opts.arch))
	  {
	    macro_build (NULL, "ror", "d,w,<", dreg, sreg, (32 - rot) & 0x1f);
	    return;
	  }
	if (rot == 0)
	  {
	    macro_build (NULL, "srl", "d,w,<", dreg, sreg, 0);
	    return;
	  }
	macro_build (NULL, "sll", "d,w,<", AT, sreg, rot);
	macro_build (NULL, "srl", "d,w,<", dreg, sreg, (0x20 - rot) & 0x1f);
	macro_build (NULL, "or", "d,v,t", dreg, dreg, AT);
      }
      break;

    case M_DROR:
      if (ISA_HAS_DROR (mips_opts.isa) || CPU_HAS_DROR (mips_opts.arch))
	{
	  macro_build (NULL, "drorv", "d,t,s", dreg, sreg, treg);
	  return;
	}
      macro_build (NULL, "dsubu", "d,v,t", AT, 0, treg);
      macro_build (NULL, "dsllv", "d,t,s", AT, sreg, AT);
      macro_build (NULL, "dsrlv", "d,t,s", dreg, sreg, treg);
      macro_build (NULL, "or", "d,v,t", dreg, dreg, AT);
      break;

    case M_ROR:
      if (ISA_HAS_ROR (mips_opts.isa) || CPU_HAS_ROR (mips_opts.arch))
	{
	  macro_build (NULL, "rorv", "d,t,s", dreg, sreg, treg);
	  return;
	}
      macro_build (NULL, "subu", "d,v,t", AT, 0, treg);
      macro_build (NULL, "sllv", "d,t,s", AT, sreg, AT);
      macro_build (NULL, "srlv", "d,t,s", dreg, sreg, treg);
      macro_build (NULL, "or", "d,v,t", dreg, dreg, AT);
      break;

    case M_DROR_I:
      {
	unsigned int rot;
	char *l, *r;

	if (imm_expr.X_op != O_constant)
	  as_bad (_("Improper rotate count"));
	rot = imm_expr.X_add_number & 0x3f;
	if (ISA_HAS_DROR (mips_opts.isa) || CPU_HAS_DROR (mips_opts.arch))
	  {
	    if (rot >= 32)
	      macro_build (NULL, "dror32", "d,w,<", dreg, sreg, rot - 32);
	    else
	      macro_build (NULL, "dror", "d,w,<", dreg, sreg, rot);
	    return;
	  }
	if (rot == 0)
	  {
	    macro_build (NULL, "dsrl", "d,w,<", dreg, sreg, 0);
	    return;
	  }
	r = (rot < 0x20) ? "dsrl" : "dsrl32";
	l = ((0x40 - rot) < 0x20) ? "dsll" : "dsll32";
	rot &= 0x1f;
	macro_build (NULL, r, "d,w,<", AT, sreg, rot);
	macro_build (NULL, l, "d,w,<", dreg, sreg, (0x20 - rot) & 0x1f);
	macro_build (NULL, "or", "d,v,t", dreg, dreg, AT);
      }
      break;

    case M_ROR_I:
      {
	unsigned int rot;

	if (imm_expr.X_op != O_constant)
	  as_bad (_("Improper rotate count"));
	rot = imm_expr.X_add_number & 0x1f;
	if (ISA_HAS_ROR (mips_opts.isa) || CPU_HAS_ROR (mips_opts.arch))
	  {
	    macro_build (NULL, "ror", "d,w,<", dreg, sreg, rot);
	    return;
	  }
	if (rot == 0)
	  {
	    macro_build (NULL, "srl", "d,w,<", dreg, sreg, 0);
	    return;
	  }
	macro_build (NULL, "srl", "d,w,<", AT, sreg, rot);
	macro_build (NULL, "sll", "d,w,<", dreg, sreg, (0x20 - rot) & 0x1f);
	macro_build (NULL, "or", "d,v,t", dreg, dreg, AT);
      }
      break;

    case M_S_DOB:
      if (mips_opts.arch == CPU_R4650)
	{
	  as_bad (_("opcode not supported on this processor"));
	  return;
	}
      assert (mips_opts.isa == ISA_MIPS1);
      /* Even on a big endian machine $fn comes before $fn+1.  We have
	 to adjust when storing to memory.  */
      macro_build (&offset_expr, "swc1", "T,o(b)",
		   target_big_endian ? treg + 1 : treg, BFD_RELOC_LO16, breg);
      offset_expr.X_add_number += 4;
      macro_build (&offset_expr, "swc1", "T,o(b)",
		   target_big_endian ? treg : treg + 1, BFD_RELOC_LO16, breg);
      return;

    case M_SEQ:
      if (sreg == 0)
	macro_build (&expr1, "sltiu", "t,r,j", dreg, treg, BFD_RELOC_LO16);
      else if (treg == 0)
	macro_build (&expr1, "sltiu", "t,r,j", dreg, sreg, BFD_RELOC_LO16);
      else
	{
	  macro_build (NULL, "xor", "d,v,t", dreg, sreg, treg);
	  macro_build (&expr1, "sltiu", "t,r,j", dreg, dreg, BFD_RELOC_LO16);
	}
      return;

    case M_SEQ_I:
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 0)
	{
	  macro_build (&expr1, "sltiu", "t,r,j", dreg, sreg, BFD_RELOC_LO16);
	  return;
	}
      if (sreg == 0)
	{
	  as_warn (_("Instruction %s: result is always false"),
		   ip->insn_mo->name);
	  move_register (dreg, 0);
	  return;
	}
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number >= 0
	  && imm_expr.X_add_number < 0x10000)
	{
	  macro_build (&imm_expr, "xori", "t,r,i", dreg, sreg, BFD_RELOC_LO16);
	  used_at = 0;
	}
      else if (imm_expr.X_op == O_constant
	       && imm_expr.X_add_number > -0x8000
	       && imm_expr.X_add_number < 0)
	{
	  imm_expr.X_add_number = -imm_expr.X_add_number;
	  macro_build (&imm_expr, HAVE_32BIT_GPRS ? "addiu" : "daddiu",
		       "t,r,j", dreg, sreg, BFD_RELOC_LO16);
	  used_at = 0;
	}
      else
	{
	  load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
	  macro_build (NULL, "xor", "d,v,t", dreg, sreg, AT);
	  used_at = 1;
	}
      macro_build (&expr1, "sltiu", "t,r,j", dreg, dreg, BFD_RELOC_LO16);
      if (used_at)
	break;
      return;

    case M_SGE:		/* sreg >= treg <==> not (sreg < treg) */
      s = "slt";
      goto sge;
    case M_SGEU:
      s = "sltu";
    sge:
      macro_build (NULL, s, "d,v,t", dreg, sreg, treg);
      macro_build (&expr1, "xori", "t,r,i", dreg, dreg, BFD_RELOC_LO16);
      return;

    case M_SGE_I:		/* sreg >= I <==> not (sreg < I) */
    case M_SGEU_I:
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number >= -0x8000
	  && imm_expr.X_add_number < 0x8000)
	{
	  macro_build (&imm_expr, mask == M_SGE_I ? "slti" : "sltiu", "t,r,j",
		       dreg, sreg, BFD_RELOC_LO16);
	  used_at = 0;
	}
      else
	{
	  load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
	  macro_build (NULL, mask == M_SGE_I ? "slt" : "sltu", "d,v,t",
		       dreg, sreg, AT);
	  used_at = 1;
	}
      macro_build (&expr1, "xori", "t,r,i", dreg, dreg, BFD_RELOC_LO16);
      if (used_at)
	break;
      return;

    case M_SGT:		/* sreg > treg  <==>  treg < sreg */
      s = "slt";
      goto sgt;
    case M_SGTU:
      s = "sltu";
    sgt:
      macro_build (NULL, s, "d,v,t", dreg, treg, sreg);
      return;

    case M_SGT_I:		/* sreg > I  <==>  I < sreg */
      s = "slt";
      goto sgti;
    case M_SGTU_I:
      s = "sltu";
    sgti:
      load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
      macro_build (NULL, s, "d,v,t", dreg, AT, sreg);
      break;

    case M_SLE:	/* sreg <= treg  <==>  treg >= sreg  <==>  not (treg < sreg) */
      s = "slt";
      goto sle;
    case M_SLEU:
      s = "sltu";
    sle:
      macro_build (NULL, s, "d,v,t", dreg, treg, sreg);
      macro_build (&expr1, "xori", "t,r,i", dreg, dreg, BFD_RELOC_LO16);
      return;

    case M_SLE_I:	/* sreg <= I <==> I >= sreg <==> not (I < sreg) */
      s = "slt";
      goto slei;
    case M_SLEU_I:
      s = "sltu";
    slei:
      load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
      macro_build (NULL, s, "d,v,t", dreg, AT, sreg);
      macro_build (&expr1, "xori", "t,r,i", dreg, dreg, BFD_RELOC_LO16);
      break;

    case M_SLT_I:
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number >= -0x8000
	  && imm_expr.X_add_number < 0x8000)
	{
	  macro_build (&imm_expr, "slti", "t,r,j", dreg, sreg, BFD_RELOC_LO16);
	  return;
	}
      load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
      macro_build (NULL, "slt", "d,v,t", dreg, sreg, AT);
      break;

    case M_SLTU_I:
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number >= -0x8000
	  && imm_expr.X_add_number < 0x8000)
	{
	  macro_build (&imm_expr, "sltiu", "t,r,j", dreg, sreg,
		       BFD_RELOC_LO16);
	  return;
	}
      load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
      macro_build (NULL, "sltu", "d,v,t", dreg, sreg, AT);
      break;

    case M_SNE:
      if (sreg == 0)
	macro_build (NULL, "sltu", "d,v,t", dreg, 0, treg);
      else if (treg == 0)
	macro_build (NULL, "sltu", "d,v,t", dreg, 0, sreg);
      else
	{
	  macro_build (NULL, "xor", "d,v,t", dreg, sreg, treg);
	  macro_build (NULL, "sltu", "d,v,t", dreg, 0, dreg);
	}
      return;

    case M_SNE_I:
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 0)
	{
	  macro_build (NULL, "sltu", "d,v,t", dreg, 0, sreg);
	  return;
	}
      if (sreg == 0)
	{
	  as_warn (_("Instruction %s: result is always true"),
		   ip->insn_mo->name);
	  macro_build (&expr1, HAVE_32BIT_GPRS ? "addiu" : "daddiu", "t,r,j",
		       dreg, 0, BFD_RELOC_LO16);
	  return;
	}
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number >= 0
	  && imm_expr.X_add_number < 0x10000)
	{
	  macro_build (&imm_expr, "xori", "t,r,i", dreg, sreg, BFD_RELOC_LO16);
	  used_at = 0;
	}
      else if (imm_expr.X_op == O_constant
	       && imm_expr.X_add_number > -0x8000
	       && imm_expr.X_add_number < 0)
	{
	  imm_expr.X_add_number = -imm_expr.X_add_number;
	  macro_build (&imm_expr, HAVE_32BIT_GPRS ? "addiu" : "daddiu",
		       "t,r,j", dreg, sreg, BFD_RELOC_LO16);
	  used_at = 0;
	}
      else
	{
	  load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
	  macro_build (NULL, "xor", "d,v,t", dreg, sreg, AT);
	  used_at = 1;
	}
      macro_build (NULL, "sltu", "d,v,t", dreg, 0, dreg);
      if (used_at)
	break;
      return;

    case M_DSUB_I:
      dbl = 1;
    case M_SUB_I:
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number > -0x8000
	  && imm_expr.X_add_number <= 0x8000)
	{
	  imm_expr.X_add_number = -imm_expr.X_add_number;
	  macro_build (&imm_expr, dbl ? "daddi" : "addi", "t,r,j",
		       dreg, sreg, BFD_RELOC_LO16);
	  return;
	}
      load_register (AT, &imm_expr, dbl);
      macro_build (NULL, dbl ? "dsub" : "sub", "d,v,t", dreg, sreg, AT);
      break;

    case M_DSUBU_I:
      dbl = 1;
    case M_SUBU_I:
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number > -0x8000
	  && imm_expr.X_add_number <= 0x8000)
	{
	  imm_expr.X_add_number = -imm_expr.X_add_number;
	  macro_build (&imm_expr, dbl ? "daddiu" : "addiu", "t,r,j",
		       dreg, sreg, BFD_RELOC_LO16);
	  return;
	}
      load_register (AT, &imm_expr, dbl);
      macro_build (NULL, dbl ? "dsubu" : "subu", "d,v,t", dreg, sreg, AT);
      break;

    case M_TEQ_I:
      s = "teq";
      goto trap;
    case M_TGE_I:
      s = "tge";
      goto trap;
    case M_TGEU_I:
      s = "tgeu";
      goto trap;
    case M_TLT_I:
      s = "tlt";
      goto trap;
    case M_TLTU_I:
      s = "tltu";
      goto trap;
    case M_TNE_I:
      s = "tne";
    trap:
      load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
      macro_build (NULL, s, "s,t", sreg, AT);
      break;

    case M_TRUNCWS:
    case M_TRUNCWD:
      assert (mips_opts.isa == ISA_MIPS1);
      sreg = (ip->insn_opcode >> 11) & 0x1f;	/* floating reg */
      dreg = (ip->insn_opcode >> 06) & 0x1f;	/* floating reg */

      /*
       * Is the double cfc1 instruction a bug in the mips assembler;
       * or is there a reason for it?
       */
      mips_emit_delays (TRUE);
      ++mips_opts.noreorder;
      mips_any_noreorder = 1;
      macro_build (NULL, "cfc1", "t,G", treg, RA);
      macro_build (NULL, "cfc1", "t,G", treg, RA);
      macro_build (NULL, "nop", "");
      expr1.X_add_number = 3;
      macro_build (&expr1, "ori", "t,r,i", AT, treg, BFD_RELOC_LO16);
      expr1.X_add_number = 2;
      macro_build (&expr1, "xori", "t,r,i", AT, AT, BFD_RELOC_LO16);
      macro_build (NULL, "ctc1", "t,G", AT, RA);
      macro_build (NULL, "nop", "");
      macro_build (NULL, mask == M_TRUNCWD ? "cvt.w.d" : "cvt.w.s", "D,S",
		   dreg, sreg);
      macro_build (NULL, "ctc1", "t,G", treg, RA);
      macro_build (NULL, "nop", "");
      --mips_opts.noreorder;
      break;

    case M_ULH:
      s = "lb";
      goto ulh;
    case M_ULHU:
      s = "lbu";
    ulh:
      if (offset_expr.X_add_number >= 0x7fff)
	as_bad (_("operand overflow"));
      if (! target_big_endian)
	++offset_expr.X_add_number;
      macro_build (&offset_expr, s, "t,o(b)", AT, BFD_RELOC_LO16, breg);
      if (! target_big_endian)
	--offset_expr.X_add_number;
      else
	++offset_expr.X_add_number;
      macro_build (&offset_expr, "lbu", "t,o(b)", treg, BFD_RELOC_LO16, breg);
      macro_build (NULL, "sll", "d,w,<", AT, AT, 8);
      macro_build (NULL, "or", "d,v,t", treg, treg, AT);
      break;

    case M_ULD:
      s = "ldl";
      s2 = "ldr";
      off = 7;
      goto ulw;
    case M_ULW:
      s = "lwl";
      s2 = "lwr";
      off = 3;
    ulw:
      if (offset_expr.X_add_number >= 0x8000 - off)
	as_bad (_("operand overflow"));
      if (treg != breg)
	tempreg = treg;
      else
	tempreg = AT;
      if (! target_big_endian)
	offset_expr.X_add_number += off;
      macro_build (&offset_expr, s, "t,o(b)", tempreg, BFD_RELOC_LO16, breg);
      if (! target_big_endian)
	offset_expr.X_add_number -= off;
      else
	offset_expr.X_add_number += off;
      macro_build (&offset_expr, s2, "t,o(b)", tempreg, BFD_RELOC_LO16, breg);

      /* If necessary, move the result in tempreg the final destination.  */
      if (treg == tempreg)
        return;
      /* Protect second load's delay slot.  */
      if (!gpr_interlocks)
	macro_build (NULL, "nop", "");
      move_register (treg, tempreg);
      break;

    case M_ULD_A:
      s = "ldl";
      s2 = "ldr";
      off = 7;
      goto ulwa;
    case M_ULW_A:
      s = "lwl";
      s2 = "lwr";
      off = 3;
    ulwa:
      used_at = 1;
      load_address (AT, &offset_expr, &used_at);
      if (breg != 0)
	macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", AT, AT, breg);
      if (! target_big_endian)
	expr1.X_add_number = off;
      else
	expr1.X_add_number = 0;
      macro_build (&expr1, s, "t,o(b)", treg, BFD_RELOC_LO16, AT);
      if (! target_big_endian)
	expr1.X_add_number = 0;
      else
	expr1.X_add_number = off;
      macro_build (&expr1, s2, "t,o(b)", treg, BFD_RELOC_LO16, AT);
      break;

    case M_ULH_A:
    case M_ULHU_A:
      used_at = 1;
      load_address (AT, &offset_expr, &used_at);
      if (breg != 0)
	macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", AT, AT, breg);
      if (target_big_endian)
	expr1.X_add_number = 0;
      macro_build (&expr1, mask == M_ULH_A ? "lb" : "lbu", "t,o(b)",
		   treg, BFD_RELOC_LO16, AT);
      if (target_big_endian)
	expr1.X_add_number = 1;
      else
	expr1.X_add_number = 0;
      macro_build (&expr1, "lbu", "t,o(b)", AT, BFD_RELOC_LO16, AT);
      macro_build (NULL, "sll", "d,w,<", treg, treg, 8);
      macro_build (NULL, "or", "d,v,t", treg, treg, AT);
      break;

    case M_USH:
      if (offset_expr.X_add_number >= 0x7fff)
	as_bad (_("operand overflow"));
      if (target_big_endian)
	++offset_expr.X_add_number;
      macro_build (&offset_expr, "sb", "t,o(b)", treg, BFD_RELOC_LO16, breg);
      macro_build (NULL, "srl", "d,w,<", AT, treg, 8);
      if (target_big_endian)
	--offset_expr.X_add_number;
      else
	++offset_expr.X_add_number;
      macro_build (&offset_expr, "sb", "t,o(b)", AT, BFD_RELOC_LO16, breg);
      break;

    case M_USD:
      s = "sdl";
      s2 = "sdr";
      off = 7;
      goto usw;
    case M_USW:
      s = "swl";
      s2 = "swr";
      off = 3;
    usw:
      if (offset_expr.X_add_number >= 0x8000 - off)
	as_bad (_("operand overflow"));
      if (! target_big_endian)
	offset_expr.X_add_number += off;
      macro_build (&offset_expr, s, "t,o(b)", treg, BFD_RELOC_LO16, breg);
      if (! target_big_endian)
	offset_expr.X_add_number -= off;
      else
	offset_expr.X_add_number += off;
      macro_build (&offset_expr, s2, "t,o(b)", treg, BFD_RELOC_LO16, breg);
      return;

    case M_USD_A:
      s = "sdl";
      s2 = "sdr";
      off = 7;
      goto uswa;
    case M_USW_A:
      s = "swl";
      s2 = "swr";
      off = 3;
    uswa:
      used_at = 1;
      load_address (AT, &offset_expr, &used_at);
      if (breg != 0)
	macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", AT, AT, breg);
      if (! target_big_endian)
	expr1.X_add_number = off;
      else
	expr1.X_add_number = 0;
      macro_build (&expr1, s, "t,o(b)", treg, BFD_RELOC_LO16, AT);
      if (! target_big_endian)
	expr1.X_add_number = 0;
      else
	expr1.X_add_number = off;
      macro_build (&expr1, s2, "t,o(b)", treg, BFD_RELOC_LO16, AT);
      break;

    case M_USH_A:
      used_at = 1;
      load_address (AT, &offset_expr, &used_at);
      if (breg != 0)
	macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", AT, AT, breg);
      if (! target_big_endian)
	expr1.X_add_number = 0;
      macro_build (&expr1, "sb", "t,o(b)", treg, BFD_RELOC_LO16, AT);
      macro_build (NULL, "srl", "d,w,<", treg, treg, 8);
      if (! target_big_endian)
	expr1.X_add_number = 1;
      else
	expr1.X_add_number = 0;
      macro_build (&expr1, "sb", "t,o(b)", treg, BFD_RELOC_LO16, AT);
      if (! target_big_endian)
	expr1.X_add_number = 0;
      else
	expr1.X_add_number = 1;
      macro_build (&expr1, "lbu", "t,o(b)", AT, BFD_RELOC_LO16, AT);
      macro_build (NULL, "sll", "d,w,<", treg, treg, 8);
      macro_build (NULL, "or", "d,v,t", treg, treg, AT);
      break;

    default:
      /* FIXME: Check if this is one of the itbl macros, since they
	 are added dynamically.  */
      as_bad (_("Macro %s not implemented yet"), ip->insn_mo->name);
      break;
    }
  if (mips_opts.noat)
    as_warn (_("Macro used $at after \".set noat\""));
}

/* Implement macros in mips16 mode.  */

static void
mips16_macro (struct mips_cl_insn *ip)
{
  int mask;
  int xreg, yreg, zreg, tmp;
  expressionS expr1;
  int dbl;
  const char *s, *s2, *s3;

  mask = ip->insn_mo->mask;

  xreg = (ip->insn_opcode >> MIPS16OP_SH_RX) & MIPS16OP_MASK_RX;
  yreg = (ip->insn_opcode >> MIPS16OP_SH_RY) & MIPS16OP_MASK_RY;
  zreg = (ip->insn_opcode >> MIPS16OP_SH_RZ) & MIPS16OP_MASK_RZ;

  expr1.X_op = O_constant;
  expr1.X_op_symbol = NULL;
  expr1.X_add_symbol = NULL;
  expr1.X_add_number = 1;

  dbl = 0;

  switch (mask)
    {
    default:
      internalError ();

    case M_DDIV_3:
      dbl = 1;
    case M_DIV_3:
      s = "mflo";
      goto do_div3;
    case M_DREM_3:
      dbl = 1;
    case M_REM_3:
      s = "mfhi";
    do_div3:
      mips_emit_delays (TRUE);
      ++mips_opts.noreorder;
      mips_any_noreorder = 1;
      macro_build (NULL, dbl ? "ddiv" : "div", "0,x,y", xreg, yreg);
      expr1.X_add_number = 2;
      macro_build (&expr1, "bnez", "x,p", yreg);
      macro_build (NULL, "break", "6", 7);

      /* FIXME: The normal code checks for of -1 / -0x80000000 here,
         since that causes an overflow.  We should do that as well,
         but I don't see how to do the comparisons without a temporary
         register.  */
      --mips_opts.noreorder;
      macro_build (NULL, s, "x", zreg);
      break;

    case M_DIVU_3:
      s = "divu";
      s2 = "mflo";
      goto do_divu3;
    case M_REMU_3:
      s = "divu";
      s2 = "mfhi";
      goto do_divu3;
    case M_DDIVU_3:
      s = "ddivu";
      s2 = "mflo";
      goto do_divu3;
    case M_DREMU_3:
      s = "ddivu";
      s2 = "mfhi";
    do_divu3:
      mips_emit_delays (TRUE);
      ++mips_opts.noreorder;
      mips_any_noreorder = 1;
      macro_build (NULL, s, "0,x,y", xreg, yreg);
      expr1.X_add_number = 2;
      macro_build (&expr1, "bnez", "x,p", yreg);
      macro_build (NULL, "break", "6", 7);
      --mips_opts.noreorder;
      macro_build (NULL, s2, "x", zreg);
      break;

    case M_DMUL:
      dbl = 1;
    case M_MUL:
      macro_build (NULL, dbl ? "dmultu" : "multu", "x,y", xreg, yreg);
      macro_build (NULL, "mflo", "x", zreg);
      return;

    case M_DSUBU_I:
      dbl = 1;
      goto do_subu;
    case M_SUBU_I:
    do_subu:
      if (imm_expr.X_op != O_constant)
	as_bad (_("Unsupported large constant"));
      imm_expr.X_add_number = -imm_expr.X_add_number;
      macro_build (&imm_expr, dbl ? "daddiu" : "addiu", "y,x,4", yreg, xreg);
      break;

    case M_SUBU_I_2:
      if (imm_expr.X_op != O_constant)
	as_bad (_("Unsupported large constant"));
      imm_expr.X_add_number = -imm_expr.X_add_number;
      macro_build (&imm_expr, "addiu", "x,k", xreg);
      break;

    case M_DSUBU_I_2:
      if (imm_expr.X_op != O_constant)
	as_bad (_("Unsupported large constant"));
      imm_expr.X_add_number = -imm_expr.X_add_number;
      macro_build (&imm_expr, "daddiu", "y,j", yreg);
      break;

    case M_BEQ:
      s = "cmp";
      s2 = "bteqz";
      goto do_branch;
    case M_BNE:
      s = "cmp";
      s2 = "btnez";
      goto do_branch;
    case M_BLT:
      s = "slt";
      s2 = "btnez";
      goto do_branch;
    case M_BLTU:
      s = "sltu";
      s2 = "btnez";
      goto do_branch;
    case M_BLE:
      s = "slt";
      s2 = "bteqz";
      goto do_reverse_branch;
    case M_BLEU:
      s = "sltu";
      s2 = "bteqz";
      goto do_reverse_branch;
    case M_BGE:
      s = "slt";
      s2 = "bteqz";
      goto do_branch;
    case M_BGEU:
      s = "sltu";
      s2 = "bteqz";
      goto do_branch;
    case M_BGT:
      s = "slt";
      s2 = "btnez";
      goto do_reverse_branch;
    case M_BGTU:
      s = "sltu";
      s2 = "btnez";

    do_reverse_branch:
      tmp = xreg;
      xreg = yreg;
      yreg = tmp;

    do_branch:
      macro_build (NULL, s, "x,y", xreg, yreg);
      macro_build (&offset_expr, s2, "p");
      break;

    case M_BEQ_I:
      s = "cmpi";
      s2 = "bteqz";
      s3 = "x,U";
      goto do_branch_i;
    case M_BNE_I:
      s = "cmpi";
      s2 = "btnez";
      s3 = "x,U";
      goto do_branch_i;
    case M_BLT_I:
      s = "slti";
      s2 = "btnez";
      s3 = "x,8";
      goto do_branch_i;
    case M_BLTU_I:
      s = "sltiu";
      s2 = "btnez";
      s3 = "x,8";
      goto do_branch_i;
    case M_BLE_I:
      s = "slti";
      s2 = "btnez";
      s3 = "x,8";
      goto do_addone_branch_i;
    case M_BLEU_I:
      s = "sltiu";
      s2 = "btnez";
      s3 = "x,8";
      goto do_addone_branch_i;
    case M_BGE_I:
      s = "slti";
      s2 = "bteqz";
      s3 = "x,8";
      goto do_branch_i;
    case M_BGEU_I:
      s = "sltiu";
      s2 = "bteqz";
      s3 = "x,8";
      goto do_branch_i;
    case M_BGT_I:
      s = "slti";
      s2 = "bteqz";
      s3 = "x,8";
      goto do_addone_branch_i;
    case M_BGTU_I:
      s = "sltiu";
      s2 = "bteqz";
      s3 = "x,8";

    do_addone_branch_i:
      if (imm_expr.X_op != O_constant)
	as_bad (_("Unsupported large constant"));
      ++imm_expr.X_add_number;

    do_branch_i:
      macro_build (&imm_expr, s, s3, xreg);
      macro_build (&offset_expr, s2, "p");
      break;

    case M_ABS:
      expr1.X_add_number = 0;
      macro_build (&expr1, "slti", "x,8", yreg);
      if (xreg != yreg)
	move_register (xreg, yreg);
      expr1.X_add_number = 2;
      macro_build (&expr1, "bteqz", "p");
      macro_build (NULL, "neg", "x,w", xreg, xreg);
    }
}

/* For consistency checking, verify that all bits are specified either
   by the match/mask part of the instruction definition, or by the
   operand list.  */
static int
validate_mips_insn (const struct mips_opcode *opc)
{
  const char *p = opc->args;
  char c;
  unsigned long used_bits = opc->mask;

  if ((used_bits & opc->match) != opc->match)
    {
      as_bad (_("internal: bad mips opcode (mask error): %s %s"),
	      opc->name, opc->args);
      return 0;
    }
#define USE_BITS(mask,shift)	(used_bits |= ((mask) << (shift)))
  while (*p)
    switch (c = *p++)
      {
      case ',': break;
      case '(': break;
      case ')': break;
      case '+':
    	switch (c = *p++)
	  {
	  case 'A': USE_BITS (OP_MASK_SHAMT,	OP_SH_SHAMT);	break;
	  case 'B': USE_BITS (OP_MASK_INSMSB,	OP_SH_INSMSB);	break;
	  case 'C': USE_BITS (OP_MASK_EXTMSBD,	OP_SH_EXTMSBD);	break;
	  case 'D': USE_BITS (OP_MASK_RD,	OP_SH_RD);
		    USE_BITS (OP_MASK_SEL,	OP_SH_SEL);	break;
	  case 'E': USE_BITS (OP_MASK_SHAMT,	OP_SH_SHAMT);	break;
	  case 'F': USE_BITS (OP_MASK_INSMSB,	OP_SH_INSMSB);	break;
	  case 'G': USE_BITS (OP_MASK_EXTMSBD,	OP_SH_EXTMSBD);	break;
	  case 'H': USE_BITS (OP_MASK_EXTMSBD,	OP_SH_EXTMSBD);	break;
	  case 'I': break;
	  default:
	    as_bad (_("internal: bad mips opcode (unknown extension operand type `+%c'): %s %s"),
		    c, opc->name, opc->args);
	    return 0;
	  }
	break;
      case '<': USE_BITS (OP_MASK_SHAMT,	OP_SH_SHAMT);	break;
      case '>':	USE_BITS (OP_MASK_SHAMT,	OP_SH_SHAMT);	break;
      case 'A': break;
      case 'B': USE_BITS (OP_MASK_CODE20,       OP_SH_CODE20);  break;
      case 'C':	USE_BITS (OP_MASK_COPZ,		OP_SH_COPZ);	break;
      case 'D':	USE_BITS (OP_MASK_FD,		OP_SH_FD);	break;
      case 'E':	USE_BITS (OP_MASK_RT,		OP_SH_RT);	break;
      case 'F': break;
      case 'G':	USE_BITS (OP_MASK_RD,		OP_SH_RD);	break;
      case 'H': USE_BITS (OP_MASK_SEL,		OP_SH_SEL);	break;
      case 'I': break;
      case 'J': USE_BITS (OP_MASK_CODE19,       OP_SH_CODE19);  break;
      case 'K':	USE_BITS (OP_MASK_RD,		OP_SH_RD);	break;
      case 'L': break;
      case 'M':	USE_BITS (OP_MASK_CCC,		OP_SH_CCC);	break;
      case 'N':	USE_BITS (OP_MASK_BCC,		OP_SH_BCC);	break;
      case 'O':	USE_BITS (OP_MASK_ALN,		OP_SH_ALN);	break;
      case 'Q':	USE_BITS (OP_MASK_VSEL,		OP_SH_VSEL);
		USE_BITS (OP_MASK_FT,		OP_SH_FT);	break;
      case 'R':	USE_BITS (OP_MASK_FR,		OP_SH_FR);	break;
      case 'S':	USE_BITS (OP_MASK_FS,		OP_SH_FS);	break;
      case 'T':	USE_BITS (OP_MASK_FT,		OP_SH_FT);	break;
      case 'V':	USE_BITS (OP_MASK_FS,		OP_SH_FS);	break;
      case 'W':	USE_BITS (OP_MASK_FT,		OP_SH_FT);	break;
      case 'X':	USE_BITS (OP_MASK_FD,		OP_SH_FD);	break;
      case 'Y':	USE_BITS (OP_MASK_FS,		OP_SH_FS);	break;
      case 'Z':	USE_BITS (OP_MASK_FT,		OP_SH_FT);	break;
      case 'a':	USE_BITS (OP_MASK_TARGET,	OP_SH_TARGET);	break;
      case 'b':	USE_BITS (OP_MASK_RS,		OP_SH_RS);	break;
      case 'c':	USE_BITS (OP_MASK_CODE,		OP_SH_CODE);	break;
      case 'd':	USE_BITS (OP_MASK_RD,		OP_SH_RD);	break;
      case 'f': break;
      case 'h':	USE_BITS (OP_MASK_PREFX,	OP_SH_PREFX);	break;
      case 'i':	USE_BITS (OP_MASK_IMMEDIATE,	OP_SH_IMMEDIATE); break;
      case 'j':	USE_BITS (OP_MASK_DELTA,	OP_SH_DELTA);	break;
      case 'k':	USE_BITS (OP_MASK_CACHE,	OP_SH_CACHE);	break;
      case 'l': break;
      case 'o': USE_BITS (OP_MASK_DELTA,	OP_SH_DELTA);	break;
      case 'p':	USE_BITS (OP_MASK_DELTA,	OP_SH_DELTA);	break;
      case 'q':	USE_BITS (OP_MASK_CODE2,	OP_SH_CODE2);	break;
      case 'r': USE_BITS (OP_MASK_RS,		OP_SH_RS);	break;
      case 's':	USE_BITS (OP_MASK_RS,		OP_SH_RS);	break;
      case 't':	USE_BITS (OP_MASK_RT,		OP_SH_RT);	break;
      case 'u':	USE_BITS (OP_MASK_IMMEDIATE,	OP_SH_IMMEDIATE); break;
      case 'v':	USE_BITS (OP_MASK_RS,		OP_SH_RS);	break;
      case 'w':	USE_BITS (OP_MASK_RT,		OP_SH_RT);	break;
      case 'x': break;
      case 'z': break;
      case 'P': USE_BITS (OP_MASK_PERFREG,	OP_SH_PERFREG);	break;
      case 'U': USE_BITS (OP_MASK_RD,           OP_SH_RD);
	        USE_BITS (OP_MASK_RT,           OP_SH_RT);	break;
      case 'e': USE_BITS (OP_MASK_VECBYTE,	OP_SH_VECBYTE);	break;
      case '%': USE_BITS (OP_MASK_VECALIGN,	OP_SH_VECALIGN); break;
      case '[': break;
      case ']': break;
      default:
	as_bad (_("internal: bad mips opcode (unknown operand type `%c'): %s %s"),
		c, opc->name, opc->args);
	return 0;
      }
#undef USE_BITS
  if (used_bits != 0xffffffff)
    {
      as_bad (_("internal: bad mips opcode (bits 0x%lx undefined): %s %s"),
	      ~used_bits & 0xffffffff, opc->name, opc->args);
      return 0;
    }
  return 1;
}

/* This routine assembles an instruction into its binary format.  As a
   side effect, it sets one of the global variables imm_reloc or
   offset_reloc to the type of relocation to do if one of the operands
   is an address expression.  */

static void
mips_ip (char *str, struct mips_cl_insn *ip)
{
  char *s;
  const char *args;
  char c = 0;
  struct mips_opcode *insn;
  char *argsStart;
  unsigned int regno;
  unsigned int lastregno = 0;
  unsigned int lastpos = 0;
  unsigned int limlo, limhi;
  char *s_reset;
  char save_c = 0;

  insn_error = NULL;

  /* If the instruction contains a '.', we first try to match an instruction
     including the '.'.  Then we try again without the '.'.  */
  insn = NULL;
  for (s = str; *s != '\0' && !ISSPACE (*s); ++s)
    continue;

  /* If we stopped on whitespace, then replace the whitespace with null for
     the call to hash_find.  Save the character we replaced just in case we
     have to re-parse the instruction.  */
  if (ISSPACE (*s))
    {
      save_c = *s;
      *s++ = '\0';
    }

  insn = (struct mips_opcode *) hash_find (op_hash, str);

  /* If we didn't find the instruction in the opcode table, try again, but
     this time with just the instruction up to, but not including the
     first '.'.  */
  if (insn == NULL)
    {
      /* Restore the character we overwrite above (if any).  */
      if (save_c)
	*(--s) = save_c;

      /* Scan up to the first '.' or whitespace.  */
      for (s = str;
	   *s != '\0' && *s != '.' && !ISSPACE (*s);
	   ++s)
	continue;

      /* If we did not find a '.', then we can quit now.  */
      if (*s != '.')
	{
	  insn_error = "unrecognized opcode";
	  return;
	}

      /* Lookup the instruction in the hash table.  */
      *s++ = '\0';
      if ((insn = (struct mips_opcode *) hash_find (op_hash, str)) == NULL)
	{
	  insn_error = "unrecognized opcode";
	  return;
	}
    }

  argsStart = s;
  for (;;)
    {
      bfd_boolean ok;

      assert (strcmp (insn->name, str) == 0);

      if (OPCODE_IS_MEMBER (insn,
			    (mips_opts.isa
			     | (file_ase_mips16 ? INSN_MIPS16 : 0)
	      		     | (mips_opts.ase_mdmx ? INSN_MDMX : 0)
			     | (mips_opts.ase_mips3d ? INSN_MIPS3D : 0)),
			    mips_opts.arch))
	ok = TRUE;
      else
	ok = FALSE;

      if (insn->pinfo != INSN_MACRO)
	{
	  if (mips_opts.arch == CPU_R4650 && (insn->pinfo & FP_D) != 0)
	    ok = FALSE;
	}

      if (! ok)
	{
	  if (insn + 1 < &mips_opcodes[NUMOPCODES]
	      && strcmp (insn->name, insn[1].name) == 0)
	    {
	      ++insn;
	      continue;
	    }
	  else
	    {
	      if (!insn_error)
		{
		  static char buf[100];
		  sprintf (buf,
			   _("opcode not supported on this processor: %s (%s)"),
			   mips_cpu_info_from_arch (mips_opts.arch)->name,
			   mips_cpu_info_from_isa (mips_opts.isa)->name);
		  insn_error = buf;
		}
	      if (save_c)
		*(--s) = save_c;
	      return;
	    }
	}

      ip->insn_mo = insn;
      ip->insn_opcode = insn->match;
      insn_error = NULL;
      for (args = insn->args;; ++args)
	{
	  int is_mdmx;

	  s += strspn (s, " \t");
	  is_mdmx = 0;
	  switch (*args)
	    {
	    case '\0':		/* end of args */
	      if (*s == '\0')
		return;
	      break;

	    case ',':
	      if (*s++ == *args)
		continue;
	      s--;
	      switch (*++args)
		{
		case 'r':
		case 'v':
		  ip->insn_opcode |= lastregno << OP_SH_RS;
		  continue;

		case 'w':
		  ip->insn_opcode |= lastregno << OP_SH_RT;
		  continue;

		case 'W':
		  ip->insn_opcode |= lastregno << OP_SH_FT;
		  continue;

		case 'V':
		  ip->insn_opcode |= lastregno << OP_SH_FS;
		  continue;
		}
	      break;

	    case '(':
	      /* Handle optional base register.
		 Either the base register is omitted or
		 we must have a left paren.  */
	      /* This is dependent on the next operand specifier
		 is a base register specification.  */
	      assert (args[1] == 'b' || args[1] == '5'
		      || args[1] == '-' || args[1] == '4');
	      if (*s == '\0')
		return;

	    case ')':		/* these must match exactly */
	    case '[':
	    case ']':
	      if (*s++ == *args)
		continue;
	      break;

	    case '+':		/* Opcode extension character.  */
	      switch (*++args)
		{
		case 'A':		/* ins/ext position, becomes LSB.  */
		  limlo = 0;
		  limhi = 31;
		  goto do_lsb;
		case 'E':
		  limlo = 32;
		  limhi = 63;
		  goto do_lsb;
do_lsb:
		  my_getExpression (&imm_expr, s);
		  check_absolute_expr (ip, &imm_expr);
		  if ((unsigned long) imm_expr.X_add_number < limlo
		      || (unsigned long) imm_expr.X_add_number > limhi)
		    {
		      as_bad (_("Improper position (%lu)"),
			      (unsigned long) imm_expr.X_add_number);
		      imm_expr.X_add_number = limlo;
		    }
		  lastpos = imm_expr.X_add_number;
		  ip->insn_opcode |= (imm_expr.X_add_number
				      & OP_MASK_SHAMT) << OP_SH_SHAMT;
		  imm_expr.X_op = O_absent;
		  s = expr_end;
		  continue;

		case 'B':		/* ins size, becomes MSB.  */
		  limlo = 1;
		  limhi = 32;
		  goto do_msb;
		case 'F':
		  limlo = 33;
		  limhi = 64;
		  goto do_msb;
do_msb:
		  my_getExpression (&imm_expr, s);
		  check_absolute_expr (ip, &imm_expr);
		  /* Check for negative input so that small negative numbers
		     will not succeed incorrectly.  The checks against
		     (pos+size) transitively check "size" itself,
		     assuming that "pos" is reasonable.  */
		  if ((long) imm_expr.X_add_number < 0
		      || ((unsigned long) imm_expr.X_add_number
			  + lastpos) < limlo
		      || ((unsigned long) imm_expr.X_add_number
			  + lastpos) > limhi)
		    {
		      as_bad (_("Improper insert size (%lu, position %lu)"),
			      (unsigned long) imm_expr.X_add_number,
			      (unsigned long) lastpos);
		      imm_expr.X_add_number = limlo - lastpos;
		    }
		  ip->insn_opcode |= ((lastpos + imm_expr.X_add_number - 1)
				      & OP_MASK_INSMSB) << OP_SH_INSMSB;
		  imm_expr.X_op = O_absent;
		  s = expr_end;
		  continue;

		case 'C':		/* ext size, becomes MSBD.  */
		  limlo = 1;
		  limhi = 32;
		  goto do_msbd;
		case 'G':
		  limlo = 33;
		  limhi = 64;
		  goto do_msbd;
		case 'H':
		  limlo = 33;
		  limhi = 64;
		  goto do_msbd;
do_msbd:
		  my_getExpression (&imm_expr, s);
		  check_absolute_expr (ip, &imm_expr);
		  /* Check for negative input so that small negative numbers
		     will not succeed incorrectly.  The checks against
		     (pos+size) transitively check "size" itself,
		     assuming that "pos" is reasonable.  */
		  if ((long) imm_expr.X_add_number < 0
		      || ((unsigned long) imm_expr.X_add_number
			  + lastpos) < limlo
		      || ((unsigned long) imm_expr.X_add_number
			  + lastpos) > limhi)
		    {
		      as_bad (_("Improper extract size (%lu, position %lu)"),
			      (unsigned long) imm_expr.X_add_number,
			      (unsigned long) lastpos);
		      imm_expr.X_add_number = limlo - lastpos;
		    }
		  ip->insn_opcode |= ((imm_expr.X_add_number - 1)
				      & OP_MASK_EXTMSBD) << OP_SH_EXTMSBD;
		  imm_expr.X_op = O_absent;
		  s = expr_end;
		  continue;

		case 'D':
		  /* +D is for disassembly only; never match.  */
		  break;

		case 'I':
		  /* "+I" is like "I", except that imm2_expr is used.  */
		  my_getExpression (&imm2_expr, s);
		  if (imm2_expr.X_op != O_big
		      && imm2_expr.X_op != O_constant)
		  insn_error = _("absolute expression required");
		  normalize_constant_expr (&imm2_expr);
		  s = expr_end;
		  continue;

		default:
		  as_bad (_("internal: bad mips opcode (unknown extension operand type `+%c'): %s %s"),
		    *args, insn->name, insn->args);
		  /* Further processing is fruitless.  */
		  return;
		}
	      break;

	    case '<':		/* must be at least one digit */
	      /*
	       * According to the manual, if the shift amount is greater
	       * than 31 or less than 0, then the shift amount should be
	       * mod 32.  In reality the mips assembler issues an error.
	       * We issue a warning and mask out all but the low 5 bits.
	       */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > 31)
		{
		  as_warn (_("Improper shift amount (%lu)"),
			   (unsigned long) imm_expr.X_add_number);
		  imm_expr.X_add_number &= OP_MASK_SHAMT;
		}
	      ip->insn_opcode |= imm_expr.X_add_number << OP_SH_SHAMT;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case '>':		/* shift amount minus 32 */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number < 32
		  || (unsigned long) imm_expr.X_add_number > 63)
		break;
	      ip->insn_opcode |= (imm_expr.X_add_number - 32) << OP_SH_SHAMT;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'k':		/* cache code */
	    case 'h':		/* prefx code */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > 31)
		{
		  as_warn (_("Invalid value for `%s' (%lu)"),
			   ip->insn_mo->name,
			   (unsigned long) imm_expr.X_add_number);
		  imm_expr.X_add_number &= 0x1f;
		}
	      if (*args == 'k')
		ip->insn_opcode |= imm_expr.X_add_number << OP_SH_CACHE;
	      else
		ip->insn_opcode |= imm_expr.X_add_number << OP_SH_PREFX;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'c':		/* break code */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > 1023)
		{
		  as_warn (_("Illegal break code (%lu)"),
			   (unsigned long) imm_expr.X_add_number);
		  imm_expr.X_add_number &= OP_MASK_CODE;
		}
	      ip->insn_opcode |= imm_expr.X_add_number << OP_SH_CODE;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'q':		/* lower break code */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > 1023)
		{
		  as_warn (_("Illegal lower break code (%lu)"),
			   (unsigned long) imm_expr.X_add_number);
		  imm_expr.X_add_number &= OP_MASK_CODE2;
		}
	      ip->insn_opcode |= imm_expr.X_add_number << OP_SH_CODE2;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'B':           /* 20-bit syscall/break code.  */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > OP_MASK_CODE20)
		as_warn (_("Illegal 20-bit code (%lu)"),
			 (unsigned long) imm_expr.X_add_number);
	      ip->insn_opcode |= imm_expr.X_add_number << OP_SH_CODE20;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'C':           /* Coprocessor code */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number >= (1 << 25))
		{
		  as_warn (_("Coproccesor code > 25 bits (%lu)"),
			   (unsigned long) imm_expr.X_add_number);
		  imm_expr.X_add_number &= ((1 << 25) - 1);
		}
	      ip->insn_opcode |= imm_expr.X_add_number;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'J':           /* 19-bit wait code.  */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > OP_MASK_CODE19)
		as_warn (_("Illegal 19-bit code (%lu)"),
			 (unsigned long) imm_expr.X_add_number);
	      ip->insn_opcode |= imm_expr.X_add_number << OP_SH_CODE19;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'P':		/* Performance register */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if (imm_expr.X_add_number != 0 && imm_expr.X_add_number != 1)
		{
		  as_warn (_("Invalid performance register (%lu)"),
			   (unsigned long) imm_expr.X_add_number);
		  imm_expr.X_add_number &= OP_MASK_PERFREG;
		}
	      ip->insn_opcode |= (imm_expr.X_add_number << OP_SH_PERFREG);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'b':		/* base register */
	    case 'd':		/* destination register */
	    case 's':		/* source register */
	    case 't':		/* target register */
	    case 'r':		/* both target and source */
	    case 'v':		/* both dest and source */
	    case 'w':		/* both dest and target */
	    case 'E':		/* coprocessor target register */
	    case 'G':		/* coprocessor destination register */
	    case 'K':		/* 'rdhwr' destination register */
	    case 'x':		/* ignore register name */
	    case 'z':		/* must be zero register */
	    case 'U':           /* destination register (clo/clz).  */
	      s_reset = s;
	      if (s[0] == '$')
		{

		  if (ISDIGIT (s[1]))
		    {
		      ++s;
		      regno = 0;
		      do
			{
			  regno *= 10;
			  regno += *s - '0';
			  ++s;
			}
		      while (ISDIGIT (*s));
		      if (regno > 31)
			as_bad (_("Invalid register number (%d)"), regno);
		    }
		  else if (*args == 'E' || *args == 'G' || *args == 'K')
		    goto notreg;
		  else
		    {
		      if (s[1] == 'r' && s[2] == 'a')
			{
			  s += 3;
			  regno = RA;
			}
		      else if (s[1] == 'f' && s[2] == 'p')
			{
			  s += 3;
			  regno = FP;
			}
		      else if (s[1] == 's' && s[2] == 'p')
			{
			  s += 3;
			  regno = SP;
			}
		      else if (s[1] == 'g' && s[2] == 'p')
			{
			  s += 3;
			  regno = GP;
			}
		      else if (s[1] == 'a' && s[2] == 't')
			{
			  s += 3;
			  regno = AT;
			}
		      else if (s[1] == 'k' && s[2] == 't' && s[3] == '0')
			{
			  s += 4;
			  regno = KT0;
			}
		      else if (s[1] == 'k' && s[2] == 't' && s[3] == '1')
			{
			  s += 4;
			  regno = KT1;
			}
		      else if (s[1] == 'z' && s[2] == 'e' && s[3] == 'r' && s[4] == 'o')
			{
			  s += 5;
			  regno = ZERO;
			}
		      else if (itbl_have_entries)
			{
			  char *p, *n;
			  unsigned long r;

			  p = s + 1; 	/* advance past '$' */
			  n = itbl_get_field (&p);  /* n is name */

			  /* See if this is a register defined in an
			     itbl entry.  */
			  if (itbl_get_reg_val (n, &r))
			    {
			      /* Get_field advances to the start of
				 the next field, so we need to back
				 rack to the end of the last field.  */
			      if (p)
				s = p - 1;
			      else
				s = strchr (s, '\0');
			      regno = r;
			    }
			  else
			    goto notreg;
			}
		      else
			goto notreg;
		    }
		  if (regno == AT
		      && ! mips_opts.noat
		      && *args != 'E'
		      && *args != 'G'
		      && *args != 'K')
		    as_warn (_("Used $at without \".set noat\""));
		  c = *args;
		  if (*s == ' ')
		    ++s;
		  if (args[1] != *s)
		    {
		      if (c == 'r' || c == 'v' || c == 'w')
			{
			  regno = lastregno;
			  s = s_reset;
			  ++args;
			}
		    }
		  /* 'z' only matches $0.  */
		  if (c == 'z' && regno != 0)
		    break;

	/* Now that we have assembled one operand, we use the args string
	 * to figure out where it goes in the instruction.  */
		  switch (c)
		    {
		    case 'r':
		    case 's':
		    case 'v':
		    case 'b':
		      ip->insn_opcode |= regno << OP_SH_RS;
		      break;
		    case 'd':
		    case 'G':
		    case 'K':
		      ip->insn_opcode |= regno << OP_SH_RD;
		      break;
		    case 'U':
		      ip->insn_opcode |= regno << OP_SH_RD;
		      ip->insn_opcode |= regno << OP_SH_RT;
		      break;
		    case 'w':
		    case 't':
		    case 'E':
		      ip->insn_opcode |= regno << OP_SH_RT;
		      break;
		    case 'x':
		      /* This case exists because on the r3000 trunc
			 expands into a macro which requires a gp
			 register.  On the r6000 or r4000 it is
			 assembled into a single instruction which
			 ignores the register.  Thus the insn version
			 is MIPS_ISA2 and uses 'x', and the macro
			 version is MIPS_ISA1 and uses 't'.  */
		      break;
		    case 'z':
		      /* This case is for the div instruction, which
			 acts differently if the destination argument
			 is $0.  This only matches $0, and is checked
			 outside the switch.  */
		      break;
		    case 'D':
		      /* Itbl operand; not yet implemented. FIXME ?? */
		      break;
		      /* What about all other operands like 'i', which
			 can be specified in the opcode table? */
		    }
		  lastregno = regno;
		  continue;
		}
	    notreg:
	      switch (*args++)
		{
		case 'r':
		case 'v':
		  ip->insn_opcode |= lastregno << OP_SH_RS;
		  continue;
		case 'w':
		  ip->insn_opcode |= lastregno << OP_SH_RT;
		  continue;
		}
	      break;

	    case 'O':		/* MDMX alignment immediate constant.  */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > OP_MASK_ALN)
		{
		  as_warn ("Improper align amount (%ld), using low bits",
			   (long) imm_expr.X_add_number);
		  imm_expr.X_add_number &= OP_MASK_ALN;
		}
	      ip->insn_opcode |= imm_expr.X_add_number << OP_SH_ALN;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'Q':		/* MDMX vector, element sel, or const.  */
	      if (s[0] != '$')
		{
		  /* MDMX Immediate.  */
		  my_getExpression (&imm_expr, s);
		  check_absolute_expr (ip, &imm_expr);
		  if ((unsigned long) imm_expr.X_add_number > OP_MASK_FT)
		    {
		      as_warn (_("Invalid MDMX Immediate (%ld)"),
			       (long) imm_expr.X_add_number);
		      imm_expr.X_add_number &= OP_MASK_FT;
		    }
		  imm_expr.X_add_number &= OP_MASK_FT;
		  if (ip->insn_opcode & (OP_MASK_VSEL << OP_SH_VSEL))
		    ip->insn_opcode |= MDMX_FMTSEL_IMM_QH << OP_SH_VSEL;
		  else
		    ip->insn_opcode |= MDMX_FMTSEL_IMM_OB << OP_SH_VSEL;
		  ip->insn_opcode |= imm_expr.X_add_number << OP_SH_FT;
		  imm_expr.X_op = O_absent;
		  s = expr_end;
		  continue;
		}
	      /* Not MDMX Immediate.  Fall through.  */
	    case 'X':           /* MDMX destination register.  */
	    case 'Y':           /* MDMX source register.  */
	    case 'Z':           /* MDMX target register.  */
	      is_mdmx = 1;
	    case 'D':		/* floating point destination register */
	    case 'S':		/* floating point source register */
	    case 'T':		/* floating point target register */
	    case 'R':		/* floating point source register */
	    case 'V':
	    case 'W':
	      s_reset = s;
	      /* Accept $fN for FP and MDMX register numbers, and in
                 addition accept $vN for MDMX register numbers.  */
	      if ((s[0] == '$' && s[1] == 'f' && ISDIGIT (s[2]))
		  || (is_mdmx != 0 && s[0] == '$' && s[1] == 'v'
		      && ISDIGIT (s[2])))
		{
		  s += 2;
		  regno = 0;
		  do
		    {
		      regno *= 10;
		      regno += *s - '0';
		      ++s;
		    }
		  while (ISDIGIT (*s));

		  if (regno > 31)
		    as_bad (_("Invalid float register number (%d)"), regno);

		  if ((regno & 1) != 0
		      && HAVE_32BIT_FPRS
		      && ! (strcmp (str, "mtc1") == 0
			    || strcmp (str, "mfc1") == 0
			    || strcmp (str, "lwc1") == 0
			    || strcmp (str, "swc1") == 0
			    || strcmp (str, "l.s") == 0
			    || strcmp (str, "s.s") == 0))
		    as_warn (_("Float register should be even, was %d"),
			     regno);

		  c = *args;
		  if (*s == ' ')
		    ++s;
		  if (args[1] != *s)
		    {
		      if (c == 'V' || c == 'W')
			{
			  regno = lastregno;
			  s = s_reset;
			  ++args;
			}
		    }
		  switch (c)
		    {
		    case 'D':
		    case 'X':
		      ip->insn_opcode |= regno << OP_SH_FD;
		      break;
		    case 'V':
		    case 'S':
		    case 'Y':
		      ip->insn_opcode |= regno << OP_SH_FS;
		      break;
		    case 'Q':
		      /* This is like 'Z', but also needs to fix the MDMX
			 vector/scalar select bits.  Note that the
			 scalar immediate case is handled above.  */
		      if (*s == '[')
			{
			  int is_qh = (ip->insn_opcode & (1 << OP_SH_VSEL));
			  int max_el = (is_qh ? 3 : 7);
			  s++;
			  my_getExpression(&imm_expr, s);
			  check_absolute_expr (ip, &imm_expr);
			  s = expr_end;
			  if (imm_expr.X_add_number > max_el)
			    as_bad(_("Bad element selector %ld"),
				   (long) imm_expr.X_add_number);
			  imm_expr.X_add_number &= max_el;
			  ip->insn_opcode |= (imm_expr.X_add_number
					      << (OP_SH_VSEL +
						  (is_qh ? 2 : 1)));
			  if (*s != ']')
			    as_warn(_("Expecting ']' found '%s'"), s);
			  else
			    s++;
			}
		      else
                        {
                          if (ip->insn_opcode & (OP_MASK_VSEL << OP_SH_VSEL))
                            ip->insn_opcode |= (MDMX_FMTSEL_VEC_QH
						<< OP_SH_VSEL);
			  else
			    ip->insn_opcode |= (MDMX_FMTSEL_VEC_OB <<
						OP_SH_VSEL);
			}
                      /* Fall through */
		    case 'W':
		    case 'T':
		    case 'Z':
		      ip->insn_opcode |= regno << OP_SH_FT;
		      break;
		    case 'R':
		      ip->insn_opcode |= regno << OP_SH_FR;
		      break;
		    }
		  lastregno = regno;
		  continue;
		}

	      switch (*args++)
		{
		case 'V':
		  ip->insn_opcode |= lastregno << OP_SH_FS;
		  continue;
		case 'W':
		  ip->insn_opcode |= lastregno << OP_SH_FT;
		  continue;
		}
	      break;

	    case 'I':
	      my_getExpression (&imm_expr, s);
	      if (imm_expr.X_op != O_big
		  && imm_expr.X_op != O_constant)
		insn_error = _("absolute expression required");
	      normalize_constant_expr (&imm_expr);
	      s = expr_end;
	      continue;

	    case 'A':
	      my_getExpression (&offset_expr, s);
	      *imm_reloc = BFD_RELOC_32;
	      s = expr_end;
	      continue;

	    case 'F':
	    case 'L':
	    case 'f':
	    case 'l':
	      {
		int f64;
		int using_gprs;
		char *save_in;
		char *err;
		unsigned char temp[8];
		int len;
		unsigned int length;
		segT seg;
		subsegT subseg;
		char *p;

		/* These only appear as the last operand in an
		   instruction, and every instruction that accepts
		   them in any variant accepts them in all variants.
		   This means we don't have to worry about backing out
		   any changes if the instruction does not match.

		   The difference between them is the size of the
		   floating point constant and where it goes.  For 'F'
		   and 'L' the constant is 64 bits; for 'f' and 'l' it
		   is 32 bits.  Where the constant is placed is based
		   on how the MIPS assembler does things:
		    F -- .rdata
		    L -- .lit8
		    f -- immediate value
		    l -- .lit4

		    The .lit4 and .lit8 sections are only used if
		    permitted by the -G argument.

		    When generating embedded PIC code, we use the
		    .lit8 section but not the .lit4 section (we can do
		    .lit4 inline easily; we need to put .lit8
		    somewhere in the data segment, and using .lit8
		    permits the linker to eventually combine identical
		    .lit8 entries).

		    The code below needs to know whether the target register
		    is 32 or 64 bits wide.  It relies on the fact 'f' and
		    'F' are used with GPR-based instructions and 'l' and
		    'L' are used with FPR-based instructions.  */

		f64 = *args == 'F' || *args == 'L';
		using_gprs = *args == 'F' || *args == 'f';

		save_in = input_line_pointer;
		input_line_pointer = s;
		err = md_atof (f64 ? 'd' : 'f', (char *) temp, &len);
		length = len;
		s = input_line_pointer;
		input_line_pointer = save_in;
		if (err != NULL && *err != '\0')
		  {
		    as_bad (_("Bad floating point constant: %s"), err);
		    memset (temp, '\0', sizeof temp);
		    length = f64 ? 8 : 4;
		  }

		assert (length == (unsigned) (f64 ? 8 : 4));

		if (*args == 'f'
		    || (*args == 'l'
			&& (! USE_GLOBAL_POINTER_OPT
			    || mips_pic == EMBEDDED_PIC
			    || g_switch_value < 4
			    || (temp[0] == 0 && temp[1] == 0)
			    || (temp[2] == 0 && temp[3] == 0))))
		  {
		    imm_expr.X_op = O_constant;
		    if (! target_big_endian)
		      imm_expr.X_add_number = bfd_getl32 (temp);
		    else
		      imm_expr.X_add_number = bfd_getb32 (temp);
		  }
		else if (length > 4
			 && ! mips_disable_float_construction
			 /* Constants can only be constructed in GPRs and
			    copied to FPRs if the GPRs are at least as wide
			    as the FPRs.  Force the constant into memory if
			    we are using 64-bit FPRs but the GPRs are only
			    32 bits wide.  */
			 && (using_gprs
			     || ! (HAVE_64BIT_FPRS && HAVE_32BIT_GPRS))
			 && ((temp[0] == 0 && temp[1] == 0)
			     || (temp[2] == 0 && temp[3] == 0))
			 && ((temp[4] == 0 && temp[5] == 0)
			     || (temp[6] == 0 && temp[7] == 0)))
		  {
		    /* The value is simple enough to load with a couple of
                       instructions.  If using 32-bit registers, set
                       imm_expr to the high order 32 bits and offset_expr to
                       the low order 32 bits.  Otherwise, set imm_expr to
                       the entire 64 bit constant.  */
		    if (using_gprs ? HAVE_32BIT_GPRS : HAVE_32BIT_FPRS)
		      {
			imm_expr.X_op = O_constant;
			offset_expr.X_op = O_constant;
			if (! target_big_endian)
			  {
			    imm_expr.X_add_number = bfd_getl32 (temp + 4);
			    offset_expr.X_add_number = bfd_getl32 (temp);
			  }
			else
			  {
			    imm_expr.X_add_number = bfd_getb32 (temp);
			    offset_expr.X_add_number = bfd_getb32 (temp + 4);
			  }
			if (offset_expr.X_add_number == 0)
			  offset_expr.X_op = O_absent;
		      }
		    else if (sizeof (imm_expr.X_add_number) > 4)
		      {
			imm_expr.X_op = O_constant;
			if (! target_big_endian)
			  imm_expr.X_add_number = bfd_getl64 (temp);
			else
			  imm_expr.X_add_number = bfd_getb64 (temp);
		      }
		    else
		      {
			imm_expr.X_op = O_big;
			imm_expr.X_add_number = 4;
			if (! target_big_endian)
			  {
			    generic_bignum[0] = bfd_getl16 (temp);
			    generic_bignum[1] = bfd_getl16 (temp + 2);
			    generic_bignum[2] = bfd_getl16 (temp + 4);
			    generic_bignum[3] = bfd_getl16 (temp + 6);
			  }
			else
			  {
			    generic_bignum[0] = bfd_getb16 (temp + 6);
			    generic_bignum[1] = bfd_getb16 (temp + 4);
			    generic_bignum[2] = bfd_getb16 (temp + 2);
			    generic_bignum[3] = bfd_getb16 (temp);
			  }
		      }
		  }
		else
		  {
		    const char *newname;
		    segT new_seg;

		    /* Switch to the right section.  */
		    seg = now_seg;
		    subseg = now_subseg;
		    switch (*args)
		      {
		      default: /* unused default case avoids warnings.  */
		      case 'L':
			newname = RDATA_SECTION_NAME;
			if ((USE_GLOBAL_POINTER_OPT && g_switch_value >= 8)
			    || mips_pic == EMBEDDED_PIC)
			  newname = ".lit8";
			break;
		      case 'F':
			if (mips_pic == EMBEDDED_PIC)
			  newname = ".lit8";
			else
			  newname = RDATA_SECTION_NAME;
			break;
		      case 'l':
			assert (!USE_GLOBAL_POINTER_OPT
				|| g_switch_value >= 4);
			newname = ".lit4";
			break;
		      }
		    new_seg = subseg_new (newname, (subsegT) 0);
		    if (OUTPUT_FLAVOR == bfd_target_elf_flavour)
		      bfd_set_section_flags (stdoutput, new_seg,
					     (SEC_ALLOC
					      | SEC_LOAD
					      | SEC_READONLY
					      | SEC_DATA));
		    frag_align (*args == 'l' ? 2 : 3, 0, 0);
		    if (OUTPUT_FLAVOR == bfd_target_elf_flavour
			&& strcmp (TARGET_OS, "elf") != 0)
		      record_alignment (new_seg, 4);
		    else
		      record_alignment (new_seg, *args == 'l' ? 2 : 3);
		    if (seg == now_seg)
		      as_bad (_("Can't use floating point insn in this section"));

		    /* Set the argument to the current address in the
		       section.  */
		    offset_expr.X_op = O_symbol;
		    offset_expr.X_add_symbol =
		      symbol_new ("L0\001", now_seg,
				  (valueT) frag_now_fix (), frag_now);
		    offset_expr.X_add_number = 0;

		    /* Put the floating point number into the section.  */
		    p = frag_more ((int) length);
		    memcpy (p, temp, length);

		    /* Switch back to the original section.  */
		    subseg_set (seg, subseg);
		  }
	      }
	      continue;

	    case 'i':		/* 16 bit unsigned immediate */
	    case 'j':		/* 16 bit signed immediate */
	      *imm_reloc = BFD_RELOC_LO16;
	      if (my_getSmallExpression (&imm_expr, imm_reloc, s) == 0)
		{
		  int more;
		  offsetT minval, maxval;

		  more = (insn + 1 < &mips_opcodes[NUMOPCODES]
			  && strcmp (insn->name, insn[1].name) == 0);

		  /* If the expression was written as an unsigned number,
		     only treat it as signed if there are no more
		     alternatives.  */
		  if (more
		      && *args == 'j'
		      && sizeof (imm_expr.X_add_number) <= 4
		      && imm_expr.X_op == O_constant
		      && imm_expr.X_add_number < 0
		      && imm_expr.X_unsigned
		      && HAVE_64BIT_GPRS)
		    break;

		  /* For compatibility with older assemblers, we accept
		     0x8000-0xffff as signed 16-bit numbers when only
		     signed numbers are allowed.  */
		  if (*args == 'i')
		    minval = 0, maxval = 0xffff;
		  else if (more)
		    minval = -0x8000, maxval = 0x7fff;
		  else
		    minval = -0x8000, maxval = 0xffff;

		  if (imm_expr.X_op != O_constant
		      || imm_expr.X_add_number < minval
		      || imm_expr.X_add_number > maxval)
		    {
		      if (more)
			break;
		      if (imm_expr.X_op == O_constant
			  || imm_expr.X_op == O_big)
			as_bad (_("expression out of range"));
		    }
		}
	      s = expr_end;
	      continue;

	    case 'o':		/* 16 bit offset */
	      /* Check whether there is only a single bracketed expression
		 left.  If so, it must be the base register and the
		 constant must be zero.  */
	      if (*s == '(' && strchr (s + 1, '(') == 0)
		{
		  offset_expr.X_op = O_constant;
		  offset_expr.X_add_number = 0;
		  continue;
		}

	      /* If this value won't fit into a 16 bit offset, then go
		 find a macro that will generate the 32 bit offset
		 code pattern.  */
	      if (my_getSmallExpression (&offset_expr, offset_reloc, s) == 0
		  && (offset_expr.X_op != O_constant
		      || offset_expr.X_add_number >= 0x8000
		      || offset_expr.X_add_number < -0x8000))
		break;

	      s = expr_end;
	      continue;

	    case 'p':		/* pc relative offset */
	      *offset_reloc = BFD_RELOC_16_PCREL_S2;
	      my_getExpression (&offset_expr, s);
	      s = expr_end;
	      continue;

	    case 'u':		/* upper 16 bits */
	      if (my_getSmallExpression (&imm_expr, imm_reloc, s) == 0
		  && imm_expr.X_op == O_constant
		  && (imm_expr.X_add_number < 0
		      || imm_expr.X_add_number >= 0x10000))
		as_bad (_("lui expression not in range 0..65535"));
	      s = expr_end;
	      continue;

	    case 'a':		/* 26 bit address */
	      my_getExpression (&offset_expr, s);
	      s = expr_end;
	      *offset_reloc = BFD_RELOC_MIPS_JMP;
	      continue;

	    case 'N':		/* 3 bit branch condition code */
	    case 'M':		/* 3 bit compare condition code */
	      if (strncmp (s, "$fcc", 4) != 0)
		break;
	      s += 4;
	      regno = 0;
	      do
		{
		  regno *= 10;
		  regno += *s - '0';
		  ++s;
		}
	      while (ISDIGIT (*s));
	      if (regno > 7)
		as_bad (_("Invalid condition code register $fcc%d"), regno);
	      if ((strcmp(str + strlen(str) - 3, ".ps") == 0
		   || strcmp(str + strlen(str) - 5, "any2f") == 0
		   || strcmp(str + strlen(str) - 5, "any2t") == 0)
		  && (regno & 1) != 0)
		as_warn(_("Condition code register should be even for %s, was %d"),
			str, regno);
	      if ((strcmp(str + strlen(str) - 5, "any4f") == 0
		   || strcmp(str + strlen(str) - 5, "any4t") == 0)
		  && (regno & 3) != 0)
		as_warn(_("Condition code register should be 0 or 4 for %s, was %d"),
			str, regno);
	      if (*args == 'N')
		ip->insn_opcode |= regno << OP_SH_BCC;
	      else
		ip->insn_opcode |= regno << OP_SH_CCC;
	      continue;

	    case 'H':
	      if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		s += 2;
	      if (ISDIGIT (*s))
		{
		  c = 0;
		  do
		    {
		      c *= 10;
		      c += *s - '0';
		      ++s;
		    }
		  while (ISDIGIT (*s));
		}
	      else
		c = 8; /* Invalid sel value.  */

	      if (c > 7)
		as_bad (_("invalid coprocessor sub-selection value (0-7)"));
	      ip->insn_opcode |= c;
	      continue;

	    case 'e':
	      /* Must be at least one digit.  */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);

	      if ((unsigned long) imm_expr.X_add_number
		  > (unsigned long) OP_MASK_VECBYTE)
		{
		  as_bad (_("bad byte vector index (%ld)"),
			   (long) imm_expr.X_add_number);
		  imm_expr.X_add_number = 0;
		}

	      ip->insn_opcode |= imm_expr.X_add_number << OP_SH_VECBYTE;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case '%':
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);

	      if ((unsigned long) imm_expr.X_add_number
		  > (unsigned long) OP_MASK_VECALIGN)
		{
		  as_bad (_("bad byte vector index (%ld)"),
			   (long) imm_expr.X_add_number);
		  imm_expr.X_add_number = 0;
		}

	      ip->insn_opcode |= imm_expr.X_add_number << OP_SH_VECALIGN;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    default:
	      as_bad (_("bad char = '%c'\n"), *args);
	      internalError ();
	    }
	  break;
	}
      /* Args don't match.  */
      if (insn + 1 < &mips_opcodes[NUMOPCODES] &&
	  !strcmp (insn->name, insn[1].name))
	{
	  ++insn;
	  s = argsStart;
	  insn_error = _("illegal operands");
	  continue;
	}
      if (save_c)
	*(--s) = save_c;
      insn_error = _("illegal operands");
      return;
    }
}

/* This routine assembles an instruction into its binary format when
   assembling for the mips16.  As a side effect, it sets one of the
   global variables imm_reloc or offset_reloc to the type of
   relocation to do if one of the operands is an address expression.
   It also sets mips16_small and mips16_ext if the user explicitly
   requested a small or extended instruction.  */

static void
mips16_ip (char *str, struct mips_cl_insn *ip)
{
  char *s;
  const char *args;
  struct mips_opcode *insn;
  char *argsstart;
  unsigned int regno;
  unsigned int lastregno = 0;
  char *s_reset;

  insn_error = NULL;

  mips16_small = FALSE;
  mips16_ext = FALSE;

  for (s = str; ISLOWER (*s); ++s)
    ;
  switch (*s)
    {
    case '\0':
      break;

    case ' ':
      *s++ = '\0';
      break;

    case '.':
      if (s[1] == 't' && s[2] == ' ')
	{
	  *s = '\0';
	  mips16_small = TRUE;
	  s += 3;
	  break;
	}
      else if (s[1] == 'e' && s[2] == ' ')
	{
	  *s = '\0';
	  mips16_ext = TRUE;
	  s += 3;
	  break;
	}
      /* Fall through.  */
    default:
      insn_error = _("unknown opcode");
      return;
    }

  if (mips_opts.noautoextend && ! mips16_ext)
    mips16_small = TRUE;

  if ((insn = (struct mips_opcode *) hash_find (mips16_op_hash, str)) == NULL)
    {
      insn_error = _("unrecognized opcode");
      return;
    }

  argsstart = s;
  for (;;)
    {
      assert (strcmp (insn->name, str) == 0);

      ip->insn_mo = insn;
      ip->insn_opcode = insn->match;
      ip->use_extend = FALSE;
      imm_expr.X_op = O_absent;
      imm_reloc[0] = BFD_RELOC_UNUSED;
      imm_reloc[1] = BFD_RELOC_UNUSED;
      imm_reloc[2] = BFD_RELOC_UNUSED;
      imm2_expr.X_op = O_absent;
      offset_expr.X_op = O_absent;
      offset_reloc[0] = BFD_RELOC_UNUSED;
      offset_reloc[1] = BFD_RELOC_UNUSED;
      offset_reloc[2] = BFD_RELOC_UNUSED;
      for (args = insn->args; 1; ++args)
	{
	  int c;

	  if (*s == ' ')
	    ++s;

	  /* In this switch statement we call break if we did not find
             a match, continue if we did find a match, or return if we
             are done.  */

	  c = *args;
	  switch (c)
	    {
	    case '\0':
	      if (*s == '\0')
		{
		  /* Stuff the immediate value in now, if we can.  */
		  if (imm_expr.X_op == O_constant
		      && *imm_reloc > BFD_RELOC_UNUSED
		      && insn->pinfo != INSN_MACRO)
		    {
		      mips16_immed (NULL, 0, *imm_reloc - BFD_RELOC_UNUSED,
				    imm_expr.X_add_number, TRUE, mips16_small,
				    mips16_ext, &ip->insn_opcode,
				    &ip->use_extend, &ip->extend);
		      imm_expr.X_op = O_absent;
		      *imm_reloc = BFD_RELOC_UNUSED;
		    }

		  return;
		}
	      break;

	    case ',':
	      if (*s++ == c)
		continue;
	      s--;
	      switch (*++args)
		{
		case 'v':
		  ip->insn_opcode |= lastregno << MIPS16OP_SH_RX;
		  continue;
		case 'w':
		  ip->insn_opcode |= lastregno << MIPS16OP_SH_RY;
		  continue;
		}
	      break;

	    case '(':
	    case ')':
	      if (*s++ == c)
		continue;
	      break;

	    case 'v':
	    case 'w':
	      if (s[0] != '$')
		{
		  if (c == 'v')
		    ip->insn_opcode |= lastregno << MIPS16OP_SH_RX;
		  else
		    ip->insn_opcode |= lastregno << MIPS16OP_SH_RY;
		  ++args;
		  continue;
		}
	      /* Fall through.  */
	    case 'x':
	    case 'y':
	    case 'z':
	    case 'Z':
	    case '0':
	    case 'S':
	    case 'R':
	    case 'X':
	    case 'Y':
	      if (s[0] != '$')
		break;
	      s_reset = s;
	      if (ISDIGIT (s[1]))
		{
		  ++s;
		  regno = 0;
		  do
		    {
		      regno *= 10;
		      regno += *s - '0';
		      ++s;
		    }
		  while (ISDIGIT (*s));
		  if (regno > 31)
		    {
		      as_bad (_("invalid register number (%d)"), regno);
		      regno = 2;
		    }
		}
	      else
		{
		  if (s[1] == 'r' && s[2] == 'a')
		    {
		      s += 3;
		      regno = RA;
		    }
		  else if (s[1] == 'f' && s[2] == 'p')
		    {
		      s += 3;
		      regno = FP;
		    }
		  else if (s[1] == 's' && s[2] == 'p')
		    {
		      s += 3;
		      regno = SP;
		    }
		  else if (s[1] == 'g' && s[2] == 'p')
		    {
		      s += 3;
		      regno = GP;
		    }
		  else if (s[1] == 'a' && s[2] == 't')
		    {
		      s += 3;
		      regno = AT;
		    }
		  else if (s[1] == 'k' && s[2] == 't' && s[3] == '0')
		    {
		      s += 4;
		      regno = KT0;
		    }
		  else if (s[1] == 'k' && s[2] == 't' && s[3] == '1')
		    {
		      s += 4;
		      regno = KT1;
		    }
		  else if (s[1] == 'z' && s[2] == 'e' && s[3] == 'r' && s[4] == 'o')
		    {
		      s += 5;
		      regno = ZERO;
		    }
		  else
		    break;
		}

	      if (*s == ' ')
		++s;
	      if (args[1] != *s)
		{
		  if (c == 'v' || c == 'w')
		    {
		      regno = mips16_to_32_reg_map[lastregno];
		      s = s_reset;
		      ++args;
		    }
		}

	      switch (c)
		{
		case 'x':
		case 'y':
		case 'z':
		case 'v':
		case 'w':
		case 'Z':
		  regno = mips32_to_16_reg_map[regno];
		  break;

		case '0':
		  if (regno != 0)
		    regno = ILLEGAL_REG;
		  break;

		case 'S':
		  if (regno != SP)
		    regno = ILLEGAL_REG;
		  break;

		case 'R':
		  if (regno != RA)
		    regno = ILLEGAL_REG;
		  break;

		case 'X':
		case 'Y':
		  if (regno == AT && ! mips_opts.noat)
		    as_warn (_("used $at without \".set noat\""));
		  break;

		default:
		  internalError ();
		}

	      if (regno == ILLEGAL_REG)
		break;

	      switch (c)
		{
		case 'x':
		case 'v':
		  ip->insn_opcode |= regno << MIPS16OP_SH_RX;
		  break;
		case 'y':
		case 'w':
		  ip->insn_opcode |= regno << MIPS16OP_SH_RY;
		  break;
		case 'z':
		  ip->insn_opcode |= regno << MIPS16OP_SH_RZ;
		  break;
		case 'Z':
		  ip->insn_opcode |= regno << MIPS16OP_SH_MOVE32Z;
		case '0':
		case 'S':
		case 'R':
		  break;
		case 'X':
		  ip->insn_opcode |= regno << MIPS16OP_SH_REGR32;
		  break;
		case 'Y':
		  regno = ((regno & 7) << 2) | ((regno & 0x18) >> 3);
		  ip->insn_opcode |= regno << MIPS16OP_SH_REG32R;
		  break;
		default:
		  internalError ();
		}

	      lastregno = regno;
	      continue;

	    case 'P':
	      if (strncmp (s, "$pc", 3) == 0)
		{
		  s += 3;
		  continue;
		}
	      break;

	    case '<':
	    case '>':
	    case '[':
	    case ']':
	    case '4':
	    case '5':
	    case 'H':
	    case 'W':
	    case 'D':
	    case 'j':
	    case '8':
	    case 'V':
	    case 'C':
	    case 'U':
	    case 'k':
	    case 'K':
	      if (s[0] == '%'
		  && strncmp (s + 1, "gprel(", sizeof "gprel(" - 1) == 0)
		{
		  /* This is %gprel(SYMBOL).  We need to read SYMBOL,
                     and generate the appropriate reloc.  If the text
                     inside %gprel is not a symbol name with an
                     optional offset, then we generate a normal reloc
                     and will probably fail later.  */
		  my_getExpression (&imm_expr, s + sizeof "%gprel" - 1);
		  if (imm_expr.X_op == O_symbol)
		    {
		      mips16_ext = TRUE;
		      *imm_reloc = BFD_RELOC_MIPS16_GPREL;
		      s = expr_end;
		      ip->use_extend = TRUE;
		      ip->extend = 0;
		      continue;
		    }
		}
	      else
		{
		  /* Just pick up a normal expression.  */
		  my_getExpression (&imm_expr, s);
		}

	      if (imm_expr.X_op == O_register)
		{
		  /* What we thought was an expression turned out to
                     be a register.  */

		  if (s[0] == '(' && args[1] == '(')
		    {
		      /* It looks like the expression was omitted
			 before a register indirection, which means
			 that the expression is implicitly zero.  We
			 still set up imm_expr, so that we handle
			 explicit extensions correctly.  */
		      imm_expr.X_op = O_constant;
		      imm_expr.X_add_number = 0;
		      *imm_reloc = (int) BFD_RELOC_UNUSED + c;
		      continue;
		    }

		  break;
		}

	      /* We need to relax this instruction.  */
	      *imm_reloc = (int) BFD_RELOC_UNUSED + c;
	      s = expr_end;
	      continue;

	    case 'p':
	    case 'q':
	    case 'A':
	    case 'B':
	    case 'E':
	      /* We use offset_reloc rather than imm_reloc for the PC
                 relative operands.  This lets macros with both
                 immediate and address operands work correctly.  */
	      my_getExpression (&offset_expr, s);

	      if (offset_expr.X_op == O_register)
		break;

	      /* We need to relax this instruction.  */
	      *offset_reloc = (int) BFD_RELOC_UNUSED + c;
	      s = expr_end;
	      continue;

	    case '6':		/* break code */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > 63)
		{
		  as_warn (_("Invalid value for `%s' (%lu)"),
			   ip->insn_mo->name,
			   (unsigned long) imm_expr.X_add_number);
		  imm_expr.X_add_number &= 0x3f;
		}
	      ip->insn_opcode |= imm_expr.X_add_number << MIPS16OP_SH_IMM6;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'a':		/* 26 bit address */
	      my_getExpression (&offset_expr, s);
	      s = expr_end;
	      *offset_reloc = BFD_RELOC_MIPS16_JMP;
	      ip->insn_opcode <<= 16;
	      continue;

	    case 'l':		/* register list for entry macro */
	    case 'L':		/* register list for exit macro */
	      {
		int mask;

		if (c == 'l')
		  mask = 0;
		else
		  mask = 7 << 3;
		while (*s != '\0')
		  {
		    int freg, reg1, reg2;

		    while (*s == ' ' || *s == ',')
		      ++s;
		    if (*s != '$')
		      {
			as_bad (_("can't parse register list"));
			break;
		      }
		    ++s;
		    if (*s != 'f')
		      freg = 0;
		    else
		      {
			freg = 1;
			++s;
		      }
		    reg1 = 0;
		    while (ISDIGIT (*s))
		      {
			reg1 *= 10;
			reg1 += *s - '0';
			++s;
		      }
		    if (*s == ' ')
		      ++s;
		    if (*s != '-')
		      reg2 = reg1;
		    else
		      {
			++s;
			if (*s != '$')
			  break;
			++s;
			if (freg)
			  {
			    if (*s == 'f')
			      ++s;
			    else
			      {
				as_bad (_("invalid register list"));
				break;
			      }
			  }
			reg2 = 0;
			while (ISDIGIT (*s))
			  {
			    reg2 *= 10;
			    reg2 += *s - '0';
			    ++s;
			  }
		      }
		    if (freg && reg1 == 0 && reg2 == 0 && c == 'L')
		      {
			mask &= ~ (7 << 3);
			mask |= 5 << 3;
		      }
		    else if (freg && reg1 == 0 && reg2 == 1 && c == 'L')
		      {
			mask &= ~ (7 << 3);
			mask |= 6 << 3;
		      }
		    else if (reg1 == 4 && reg2 >= 4 && reg2 <= 7 && c != 'L')
		      mask |= (reg2 - 3) << 3;
		    else if (reg1 == 16 && reg2 >= 16 && reg2 <= 17)
		      mask |= (reg2 - 15) << 1;
		    else if (reg1 == RA && reg2 == RA)
		      mask |= 1;
		    else
		      {
			as_bad (_("invalid register list"));
			break;
		      }
		  }
		/* The mask is filled in in the opcode table for the
                   benefit of the disassembler.  We remove it before
                   applying the actual mask.  */
		ip->insn_opcode &= ~ ((7 << 3) << MIPS16OP_SH_IMM6);
		ip->insn_opcode |= mask << MIPS16OP_SH_IMM6;
	      }
	    continue;

	    case 'e':		/* extend code */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > 0x7ff)
		{
		  as_warn (_("Invalid value for `%s' (%lu)"),
			   ip->insn_mo->name,
			   (unsigned long) imm_expr.X_add_number);
		  imm_expr.X_add_number &= 0x7ff;
		}
	      ip->insn_opcode |= imm_expr.X_add_number;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    default:
	      internalError ();
	    }
	  break;
	}

      /* Args don't match.  */
      if (insn + 1 < &mips16_opcodes[bfd_mips16_num_opcodes] &&
	  strcmp (insn->name, insn[1].name) == 0)
	{
	  ++insn;
	  s = argsstart;
	  continue;
	}

      insn_error = _("illegal operands");

      return;
    }
}

/* This structure holds information we know about a mips16 immediate
   argument type.  */

struct mips16_immed_operand
{
  /* The type code used in the argument string in the opcode table.  */
  int type;
  /* The number of bits in the short form of the opcode.  */
  int nbits;
  /* The number of bits in the extended form of the opcode.  */
  int extbits;
  /* The amount by which the short form is shifted when it is used;
     for example, the sw instruction has a shift count of 2.  */
  int shift;
  /* The amount by which the short form is shifted when it is stored
     into the instruction code.  */
  int op_shift;
  /* Non-zero if the short form is unsigned.  */
  int unsp;
  /* Non-zero if the extended form is unsigned.  */
  int extu;
  /* Non-zero if the value is PC relative.  */
  int pcrel;
};

/* The mips16 immediate operand types.  */

static const struct mips16_immed_operand mips16_immed_operands[] =
{
  { '<',  3,  5, 0, MIPS16OP_SH_RZ,   1, 1, 0 },
  { '>',  3,  5, 0, MIPS16OP_SH_RX,   1, 1, 0 },
  { '[',  3,  6, 0, MIPS16OP_SH_RZ,   1, 1, 0 },
  { ']',  3,  6, 0, MIPS16OP_SH_RX,   1, 1, 0 },
  { '4',  4, 15, 0, MIPS16OP_SH_IMM4, 0, 0, 0 },
  { '5',  5, 16, 0, MIPS16OP_SH_IMM5, 1, 0, 0 },
  { 'H',  5, 16, 1, MIPS16OP_SH_IMM5, 1, 0, 0 },
  { 'W',  5, 16, 2, MIPS16OP_SH_IMM5, 1, 0, 0 },
  { 'D',  5, 16, 3, MIPS16OP_SH_IMM5, 1, 0, 0 },
  { 'j',  5, 16, 0, MIPS16OP_SH_IMM5, 0, 0, 0 },
  { '8',  8, 16, 0, MIPS16OP_SH_IMM8, 1, 0, 0 },
  { 'V',  8, 16, 2, MIPS16OP_SH_IMM8, 1, 0, 0 },
  { 'C',  8, 16, 3, MIPS16OP_SH_IMM8, 1, 0, 0 },
  { 'U',  8, 16, 0, MIPS16OP_SH_IMM8, 1, 1, 0 },
  { 'k',  8, 16, 0, MIPS16OP_SH_IMM8, 0, 0, 0 },
  { 'K',  8, 16, 3, MIPS16OP_SH_IMM8, 0, 0, 0 },
  { 'p',  8, 16, 0, MIPS16OP_SH_IMM8, 0, 0, 1 },
  { 'q', 11, 16, 0, MIPS16OP_SH_IMM8, 0, 0, 1 },
  { 'A',  8, 16, 2, MIPS16OP_SH_IMM8, 1, 0, 1 },
  { 'B',  5, 16, 3, MIPS16OP_SH_IMM5, 1, 0, 1 },
  { 'E',  5, 16, 2, MIPS16OP_SH_IMM5, 1, 0, 1 }
};

#define MIPS16_NUM_IMMED \
  (sizeof mips16_immed_operands / sizeof mips16_immed_operands[0])

/* Handle a mips16 instruction with an immediate value.  This or's the
   small immediate value into *INSN.  It sets *USE_EXTEND to indicate
   whether an extended value is needed; if one is needed, it sets
   *EXTEND to the value.  The argument type is TYPE.  The value is VAL.
   If SMALL is true, an unextended opcode was explicitly requested.
   If EXT is true, an extended opcode was explicitly requested.  If
   WARN is true, warn if EXT does not match reality.  */

static void
mips16_immed (char *file, unsigned int line, int type, offsetT val,
	      bfd_boolean warn, bfd_boolean small, bfd_boolean ext,
	      unsigned long *insn, bfd_boolean *use_extend,
	      unsigned short *extend)
{
  register const struct mips16_immed_operand *op;
  int mintiny, maxtiny;
  bfd_boolean needext;

  op = mips16_immed_operands;
  while (op->type != type)
    {
      ++op;
      assert (op < mips16_immed_operands + MIPS16_NUM_IMMED);
    }

  if (op->unsp)
    {
      if (type == '<' || type == '>' || type == '[' || type == ']')
	{
	  mintiny = 1;
	  maxtiny = 1 << op->nbits;
	}
      else
	{
	  mintiny = 0;
	  maxtiny = (1 << op->nbits) - 1;
	}
    }
  else
    {
      mintiny = - (1 << (op->nbits - 1));
      maxtiny = (1 << (op->nbits - 1)) - 1;
    }

  /* Branch offsets have an implicit 0 in the lowest bit.  */
  if (type == 'p' || type == 'q')
    val /= 2;

  if ((val & ((1 << op->shift) - 1)) != 0
      || val < (mintiny << op->shift)
      || val > (maxtiny << op->shift))
    needext = TRUE;
  else
    needext = FALSE;

  if (warn && ext && ! needext)
    as_warn_where (file, line,
		   _("extended operand requested but not required"));
  if (small && needext)
    as_bad_where (file, line, _("invalid unextended operand value"));

  if (small || (! ext && ! needext))
    {
      int insnval;

      *use_extend = FALSE;
      insnval = ((val >> op->shift) & ((1 << op->nbits) - 1));
      insnval <<= op->op_shift;
      *insn |= insnval;
    }
  else
    {
      long minext, maxext;
      int extval;

      if (op->extu)
	{
	  minext = 0;
	  maxext = (1 << op->extbits) - 1;
	}
      else
	{
	  minext = - (1 << (op->extbits - 1));
	  maxext = (1 << (op->extbits - 1)) - 1;
	}
      if (val < minext || val > maxext)
	as_bad_where (file, line,
		      _("operand value out of range for instruction"));

      *use_extend = TRUE;
      if (op->extbits == 16)
	{
	  extval = ((val >> 11) & 0x1f) | (val & 0x7e0);
	  val &= 0x1f;
	}
      else if (op->extbits == 15)
	{
	  extval = ((val >> 11) & 0xf) | (val & 0x7f0);
	  val &= 0xf;
	}
      else
	{
	  extval = ((val & 0x1f) << 6) | (val & 0x20);
	  val = 0;
	}

      *extend = (unsigned short) extval;
      *insn |= val;
    }
}

static const struct percent_op_match
{
  const char *str;
  bfd_reloc_code_real_type reloc;
} percent_op[] =
{
  {"%lo", BFD_RELOC_LO16},
#ifdef OBJ_ELF
  {"%call_hi", BFD_RELOC_MIPS_CALL_HI16},
  {"%call_lo", BFD_RELOC_MIPS_CALL_LO16},
  {"%call16", BFD_RELOC_MIPS_CALL16},
  {"%got_disp", BFD_RELOC_MIPS_GOT_DISP},
  {"%got_page", BFD_RELOC_MIPS_GOT_PAGE},
  {"%got_ofst", BFD_RELOC_MIPS_GOT_OFST},
  {"%got_hi", BFD_RELOC_MIPS_GOT_HI16},
  {"%got_lo", BFD_RELOC_MIPS_GOT_LO16},
  {"%got", BFD_RELOC_MIPS_GOT16},
  {"%gp_rel", BFD_RELOC_GPREL16},
  {"%half", BFD_RELOC_16},
  {"%highest", BFD_RELOC_MIPS_HIGHEST},
  {"%higher", BFD_RELOC_MIPS_HIGHER},
  {"%neg", BFD_RELOC_MIPS_SUB},
#endif
  {"%hi", BFD_RELOC_HI16_S}
};


/* Return true if *STR points to a relocation operator.  When returning true,
   move *STR over the operator and store its relocation code in *RELOC.
   Leave both *STR and *RELOC alone when returning false.  */

static bfd_boolean
parse_relocation (char **str, bfd_reloc_code_real_type *reloc)
{
  size_t i;

  for (i = 0; i < ARRAY_SIZE (percent_op); i++)
    if (strncasecmp (*str, percent_op[i].str, strlen (percent_op[i].str)) == 0)
      {
	*str += strlen (percent_op[i].str);
	*reloc = percent_op[i].reloc;

	/* Check whether the output BFD supports this relocation.
	   If not, issue an error and fall back on something safe.  */
	if (!bfd_reloc_type_lookup (stdoutput, percent_op[i].reloc))
	  {
	    as_bad ("relocation %s isn't supported by the current ABI",
		    percent_op[i].str);
	    *reloc = BFD_RELOC_LO16;
	  }
	return TRUE;
      }
  return FALSE;
}


/* Parse string STR as a 16-bit relocatable operand.  Store the
   expression in *EP and the relocations in the array starting
   at RELOC.  Return the number of relocation operators used.

   On exit, EXPR_END points to the first character after the expression.
   If no relocation operators are used, RELOC[0] is set to BFD_RELOC_LO16.  */

static size_t
my_getSmallExpression (expressionS *ep, bfd_reloc_code_real_type *reloc,
		       char *str)
{
  bfd_reloc_code_real_type reversed_reloc[3];
  size_t reloc_index, i;
  int crux_depth, str_depth;
  char *crux;

  /* Search for the start of the main expression, recoding relocations
     in REVERSED_RELOC.  End the loop with CRUX pointing to the start
     of the main expression and with CRUX_DEPTH containing the number
     of open brackets at that point.  */
  reloc_index = -1;
  str_depth = 0;
  do
    {
      reloc_index++;
      crux = str;
      crux_depth = str_depth;

      /* Skip over whitespace and brackets, keeping count of the number
	 of brackets.  */
      while (*str == ' ' || *str == '\t' || *str == '(')
	if (*str++ == '(')
	  str_depth++;
    }
  while (*str == '%'
	 && reloc_index < (HAVE_NEWABI ? 3 : 1)
	 && parse_relocation (&str, &reversed_reloc[reloc_index]));

  my_getExpression (ep, crux);
  str = expr_end;

  /* Match every open bracket.  */
  while (crux_depth > 0 && (*str == ')' || *str == ' ' || *str == '\t'))
    if (*str++ == ')')
      crux_depth--;

  if (crux_depth > 0)
    as_bad ("unclosed '('");

  expr_end = str;

  if (reloc_index == 0)
    reloc[0] = BFD_RELOC_LO16;
  else
    {
      prev_reloc_op_frag = frag_now;
      for (i = 0; i < reloc_index; i++)
	reloc[i] = reversed_reloc[reloc_index - 1 - i];
    }

  return reloc_index;
}

static void
my_getExpression (expressionS *ep, char *str)
{
  char *save_in;
  valueT val;

  save_in = input_line_pointer;
  input_line_pointer = str;
  expression (ep);
  expr_end = input_line_pointer;
  input_line_pointer = save_in;

  /* If we are in mips16 mode, and this is an expression based on `.',
     then we bump the value of the symbol by 1 since that is how other
     text symbols are handled.  We don't bother to handle complex
     expressions, just `.' plus or minus a constant.  */
  if (mips_opts.mips16
      && ep->X_op == O_symbol
      && strcmp (S_GET_NAME (ep->X_add_symbol), FAKE_LABEL_NAME) == 0
      && S_GET_SEGMENT (ep->X_add_symbol) == now_seg
      && symbol_get_frag (ep->X_add_symbol) == frag_now
      && symbol_constant_p (ep->X_add_symbol)
      && (val = S_GET_VALUE (ep->X_add_symbol)) == frag_now_fix ())
    S_SET_VALUE (ep->X_add_symbol, val + 1);
}

/* Turn a string in input_line_pointer into a floating point constant
   of type TYPE, and store the appropriate bytes in *LITP.  The number
   of LITTLENUMS emitted is stored in *SIZEP.  An error message is
   returned, or NULL on OK.  */

char *
md_atof (int type, char *litP, int *sizeP)
{
  int prec;
  LITTLENUM_TYPE words[4];
  char *t;
  int i;

  switch (type)
    {
    case 'f':
      prec = 2;
      break;

    case 'd':
      prec = 4;
      break;

    default:
      *sizeP = 0;
      return _("bad call to md_atof");
    }

  t = atof_ieee (input_line_pointer, type, words);
  if (t)
    input_line_pointer = t;

  *sizeP = prec * 2;

  if (! target_big_endian)
    {
      for (i = prec - 1; i >= 0; i--)
	{
	  md_number_to_chars (litP, words[i], 2);
	  litP += 2;
	}
    }
  else
    {
      for (i = 0; i < prec; i++)
	{
	  md_number_to_chars (litP, words[i], 2);
	  litP += 2;
	}
    }

  return NULL;
}

void
md_number_to_chars (char *buf, valueT val, int n)
{
  if (target_big_endian)
    number_to_chars_bigendian (buf, val, n);
  else
    number_to_chars_littleendian (buf, val, n);
}

#ifdef OBJ_ELF
static int support_64bit_objects(void)
{
  const char **list, **l;
  int yes;

  list = bfd_target_list ();
  for (l = list; *l != NULL; l++)
#ifdef TE_TMIPS
    /* This is traditional mips */
    if (strcmp (*l, "elf64-tradbigmips") == 0
	|| strcmp (*l, "elf64-tradlittlemips") == 0)
#else
    if (strcmp (*l, "elf64-bigmips") == 0
	|| strcmp (*l, "elf64-littlemips") == 0)
#endif
      break;
  yes = (*l != NULL);
  free (list);
  return yes;
}
#endif /* OBJ_ELF */

const char *md_shortopts = "O::g::G:";

struct option md_longopts[] =
{
  /* Options which specify architecture.  */
#define OPTION_ARCH_BASE    (OPTION_MD_BASE)
#define OPTION_MARCH (OPTION_ARCH_BASE + 0)
  {"march", required_argument, NULL, OPTION_MARCH},
#define OPTION_MTUNE (OPTION_ARCH_BASE + 1)
  {"mtune", required_argument, NULL, OPTION_MTUNE},
#define OPTION_MIPS1 (OPTION_ARCH_BASE + 2)
  {"mips0", no_argument, NULL, OPTION_MIPS1},
  {"mips1", no_argument, NULL, OPTION_MIPS1},
#define OPTION_MIPS2 (OPTION_ARCH_BASE + 3)
  {"mips2", no_argument, NULL, OPTION_MIPS2},
#define OPTION_MIPS3 (OPTION_ARCH_BASE + 4)
  {"mips3", no_argument, NULL, OPTION_MIPS3},
#define OPTION_MIPS4 (OPTION_ARCH_BASE + 5)
  {"mips4", no_argument, NULL, OPTION_MIPS4},
#define OPTION_MIPS5 (OPTION_ARCH_BASE + 6)
  {"mips5", no_argument, NULL, OPTION_MIPS5},
#define OPTION_MIPS32 (OPTION_ARCH_BASE + 7)
  {"mips32", no_argument, NULL, OPTION_MIPS32},
#define OPTION_MIPS64 (OPTION_ARCH_BASE + 8)
  {"mips64", no_argument, NULL, OPTION_MIPS64},
#define OPTION_MIPS32R2 (OPTION_ARCH_BASE + 9)
  {"mips32r2", no_argument, NULL, OPTION_MIPS32R2},
#define OPTION_MIPS64R2 (OPTION_ARCH_BASE + 10)
  {"mips64r2", no_argument, NULL, OPTION_MIPS64R2},

  /* Options which specify Application Specific Extensions (ASEs).  */
#define OPTION_ASE_BASE (OPTION_ARCH_BASE + 11)
#define OPTION_MIPS16 (OPTION_ASE_BASE + 0)
  {"mips16", no_argument, NULL, OPTION_MIPS16},
#define OPTION_NO_MIPS16 (OPTION_ASE_BASE + 1)
  {"no-mips16", no_argument, NULL, OPTION_NO_MIPS16},
#define OPTION_MIPS3D (OPTION_ASE_BASE + 2)
  {"mips3d", no_argument, NULL, OPTION_MIPS3D},
#define OPTION_NO_MIPS3D (OPTION_ASE_BASE + 3)
  {"no-mips3d", no_argument, NULL, OPTION_NO_MIPS3D},
#define OPTION_MDMX (OPTION_ASE_BASE + 4)
  {"mdmx", no_argument, NULL, OPTION_MDMX},
#define OPTION_NO_MDMX (OPTION_ASE_BASE + 5)
  {"no-mdmx", no_argument, NULL, OPTION_NO_MDMX},

  /* Old-style architecture options.  Don't add more of these.  */
#define OPTION_COMPAT_ARCH_BASE (OPTION_ASE_BASE + 6)
#define OPTION_M4650 (OPTION_COMPAT_ARCH_BASE + 0)
  {"m4650", no_argument, NULL, OPTION_M4650},
#define OPTION_NO_M4650 (OPTION_COMPAT_ARCH_BASE + 1)
  {"no-m4650", no_argument, NULL, OPTION_NO_M4650},
#define OPTION_M4010 (OPTION_COMPAT_ARCH_BASE + 2)
  {"m4010", no_argument, NULL, OPTION_M4010},
#define OPTION_NO_M4010 (OPTION_COMPAT_ARCH_BASE + 3)
  {"no-m4010", no_argument, NULL, OPTION_NO_M4010},
#define OPTION_M4100 (OPTION_COMPAT_ARCH_BASE + 4)
  {"m4100", no_argument, NULL, OPTION_M4100},
#define OPTION_NO_M4100 (OPTION_COMPAT_ARCH_BASE + 5)
  {"no-m4100", no_argument, NULL, OPTION_NO_M4100},
#define OPTION_M3900 (OPTION_COMPAT_ARCH_BASE + 6)
  {"m3900", no_argument, NULL, OPTION_M3900},
#define OPTION_NO_M3900 (OPTION_COMPAT_ARCH_BASE + 7)
  {"no-m3900", no_argument, NULL, OPTION_NO_M3900},

  /* Options which enable bug fixes.  */
#define OPTION_FIX_BASE    (OPTION_COMPAT_ARCH_BASE + 8)
#define OPTION_M7000_HILO_FIX (OPTION_FIX_BASE + 0)
  {"mfix7000", no_argument, NULL, OPTION_M7000_HILO_FIX},
#define OPTION_MNO_7000_HILO_FIX (OPTION_FIX_BASE + 1)
  {"no-fix-7000", no_argument, NULL, OPTION_MNO_7000_HILO_FIX},
  {"mno-fix7000", no_argument, NULL, OPTION_MNO_7000_HILO_FIX},
#define OPTION_FIX_VR4122 (OPTION_FIX_BASE + 2)
#define OPTION_NO_FIX_VR4122 (OPTION_FIX_BASE + 3)
  {"mfix-vr4122-bugs",    no_argument, NULL, OPTION_FIX_VR4122},
  {"no-mfix-vr4122-bugs", no_argument, NULL, OPTION_NO_FIX_VR4122},

  /* Miscellaneous options.  */
#define OPTION_MISC_BASE (OPTION_FIX_BASE + 4)
#define OPTION_MEMBEDDED_PIC (OPTION_MISC_BASE + 0)
  {"membedded-pic", no_argument, NULL, OPTION_MEMBEDDED_PIC},
#define OPTION_TRAP (OPTION_MISC_BASE + 1)
  {"trap", no_argument, NULL, OPTION_TRAP},
  {"no-break", no_argument, NULL, OPTION_TRAP},
#define OPTION_BREAK (OPTION_MISC_BASE + 2)
  {"break", no_argument, NULL, OPTION_BREAK},
  {"no-trap", no_argument, NULL, OPTION_BREAK},
#define OPTION_EB (OPTION_MISC_BASE + 3)
  {"EB", no_argument, NULL, OPTION_EB},
#define OPTION_EL (OPTION_MISC_BASE + 4)
  {"EL", no_argument, NULL, OPTION_EL},
#define OPTION_FP32 (OPTION_MISC_BASE + 5)
  {"mfp32", no_argument, NULL, OPTION_FP32},
#define OPTION_GP32 (OPTION_MISC_BASE + 6)
  {"mgp32", no_argument, NULL, OPTION_GP32},
#define OPTION_CONSTRUCT_FLOATS (OPTION_MISC_BASE + 7)
  {"construct-floats", no_argument, NULL, OPTION_CONSTRUCT_FLOATS},
#define OPTION_NO_CONSTRUCT_FLOATS (OPTION_MISC_BASE + 8)
  {"no-construct-floats", no_argument, NULL, OPTION_NO_CONSTRUCT_FLOATS},
#define OPTION_FP64 (OPTION_MISC_BASE + 9)
  {"mfp64", no_argument, NULL, OPTION_FP64},
#define OPTION_GP64 (OPTION_MISC_BASE + 10)
  {"mgp64", no_argument, NULL, OPTION_GP64},
#define OPTION_RELAX_BRANCH (OPTION_MISC_BASE + 11)
#define OPTION_NO_RELAX_BRANCH (OPTION_MISC_BASE + 12)
  {"relax-branch", no_argument, NULL, OPTION_RELAX_BRANCH},
  {"no-relax-branch", no_argument, NULL, OPTION_NO_RELAX_BRANCH},

  /* ELF-specific options.  */
#ifdef OBJ_ELF
#define OPTION_ELF_BASE    (OPTION_MISC_BASE + 13)
#define OPTION_CALL_SHARED (OPTION_ELF_BASE + 0)
  {"KPIC",        no_argument, NULL, OPTION_CALL_SHARED},
  {"call_shared", no_argument, NULL, OPTION_CALL_SHARED},
#define OPTION_NON_SHARED  (OPTION_ELF_BASE + 1)
  {"non_shared",  no_argument, NULL, OPTION_NON_SHARED},
#define OPTION_XGOT        (OPTION_ELF_BASE + 2)
  {"xgot",        no_argument, NULL, OPTION_XGOT},
#define OPTION_MABI        (OPTION_ELF_BASE + 3)
  {"mabi", required_argument, NULL, OPTION_MABI},
#define OPTION_32 	   (OPTION_ELF_BASE + 4)
  {"32",          no_argument, NULL, OPTION_32},
#define OPTION_N32 	   (OPTION_ELF_BASE + 5)
  {"n32",         no_argument, NULL, OPTION_N32},
#define OPTION_64          (OPTION_ELF_BASE + 6)
  {"64",          no_argument, NULL, OPTION_64},
#define OPTION_MDEBUG      (OPTION_ELF_BASE + 7)
  {"mdebug", no_argument, NULL, OPTION_MDEBUG},
#define OPTION_NO_MDEBUG   (OPTION_ELF_BASE + 8)
  {"no-mdebug", no_argument, NULL, OPTION_NO_MDEBUG},
#define OPTION_PDR	   (OPTION_ELF_BASE + 9)
  {"mpdr", no_argument, NULL, OPTION_PDR},
#define OPTION_NO_PDR	   (OPTION_ELF_BASE + 10)
  {"mno-pdr", no_argument, NULL, OPTION_NO_PDR},
#endif /* OBJ_ELF */

  {NULL, no_argument, NULL, 0}
};
size_t md_longopts_size = sizeof (md_longopts);

/* Set STRING_PTR (either &mips_arch_string or &mips_tune_string) to
   NEW_VALUE.  Warn if another value was already specified.  Note:
   we have to defer parsing the -march and -mtune arguments in order
   to handle 'from-abi' correctly, since the ABI might be specified
   in a later argument.  */

static void
mips_set_option_string (const char **string_ptr, const char *new_value)
{
  if (*string_ptr != 0 && strcasecmp (*string_ptr, new_value) != 0)
    as_warn (_("A different %s was already specified, is now %s"),
	     string_ptr == &mips_arch_string ? "-march" : "-mtune",
	     new_value);

  *string_ptr = new_value;
}

int
md_parse_option (int c, char *arg)
{
  switch (c)
    {
    case OPTION_CONSTRUCT_FLOATS:
      mips_disable_float_construction = 0;
      break;

    case OPTION_NO_CONSTRUCT_FLOATS:
      mips_disable_float_construction = 1;
      break;

    case OPTION_TRAP:
      mips_trap = 1;
      break;

    case OPTION_BREAK:
      mips_trap = 0;
      break;

    case OPTION_EB:
      target_big_endian = 1;
      break;

    case OPTION_EL:
      target_big_endian = 0;
      break;

    case 'O':
      if (arg && arg[1] == '0')
	mips_optimize = 1;
      else
	mips_optimize = 2;
      break;

    case 'g':
      if (arg == NULL)
	mips_debug = 2;
      else
	mips_debug = atoi (arg);
      /* When the MIPS assembler sees -g or -g2, it does not do
         optimizations which limit full symbolic debugging.  We take
         that to be equivalent to -O0.  */
      if (mips_debug == 2)
	mips_optimize = 1;
      break;

    case OPTION_MIPS1:
      file_mips_isa = ISA_MIPS1;
      break;

    case OPTION_MIPS2:
      file_mips_isa = ISA_MIPS2;
      break;

    case OPTION_MIPS3:
      file_mips_isa = ISA_MIPS3;
      break;

    case OPTION_MIPS4:
      file_mips_isa = ISA_MIPS4;
      break;

    case OPTION_MIPS5:
      file_mips_isa = ISA_MIPS5;
      break;

    case OPTION_MIPS32:
      file_mips_isa = ISA_MIPS32;
      break;

    case OPTION_MIPS32R2:
      file_mips_isa = ISA_MIPS32R2;
      break;

    case OPTION_MIPS64R2:
      file_mips_isa = ISA_MIPS64R2;
      break;

    case OPTION_MIPS64:
      file_mips_isa = ISA_MIPS64;
      break;

    case OPTION_MTUNE:
      mips_set_option_string (&mips_tune_string, arg);
      break;

    case OPTION_MARCH:
      mips_set_option_string (&mips_arch_string, arg);
      break;

    case OPTION_M4650:
      mips_set_option_string (&mips_arch_string, "4650");
      mips_set_option_string (&mips_tune_string, "4650");
      break;

    case OPTION_NO_M4650:
      break;

    case OPTION_M4010:
      mips_set_option_string (&mips_arch_string, "4010");
      mips_set_option_string (&mips_tune_string, "4010");
      break;

    case OPTION_NO_M4010:
      break;

    case OPTION_M4100:
      mips_set_option_string (&mips_arch_string, "4100");
      mips_set_option_string (&mips_tune_string, "4100");
      break;

    case OPTION_NO_M4100:
      break;

    case OPTION_M3900:
      mips_set_option_string (&mips_arch_string, "3900");
      mips_set_option_string (&mips_tune_string, "3900");
      break;

    case OPTION_NO_M3900:
      break;

    case OPTION_MDMX:
      mips_opts.ase_mdmx = 1;
      break;

    case OPTION_NO_MDMX:
      mips_opts.ase_mdmx = 0;
      break;

    case OPTION_MIPS16:
      mips_opts.mips16 = 1;
      mips_no_prev_insn (FALSE);
      break;

    case OPTION_NO_MIPS16:
      mips_opts.mips16 = 0;
      mips_no_prev_insn (FALSE);
      break;

    case OPTION_MIPS3D:
      mips_opts.ase_mips3d = 1;
      break;

    case OPTION_NO_MIPS3D:
      mips_opts.ase_mips3d = 0;
      break;

    case OPTION_MEMBEDDED_PIC:
      mips_pic = EMBEDDED_PIC;
      if (USE_GLOBAL_POINTER_OPT && g_switch_seen)
	{
	  as_bad (_("-G may not be used with embedded PIC code"));
	  return 0;
	}
      g_switch_value = 0x7fffffff;
      break;

    case OPTION_FIX_VR4122:
      mips_fix_4122_bugs = 1;
      break;

    case OPTION_NO_FIX_VR4122:
      mips_fix_4122_bugs = 0;
      break;

    case OPTION_RELAX_BRANCH:
      mips_relax_branch = 1;
      break;

    case OPTION_NO_RELAX_BRANCH:
      mips_relax_branch = 0;
      break;

#ifdef OBJ_ELF
      /* When generating ELF code, we permit -KPIC and -call_shared to
	 select SVR4_PIC, and -non_shared to select no PIC.  This is
	 intended to be compatible with Irix 5.  */
    case OPTION_CALL_SHARED:
      if (OUTPUT_FLAVOR != bfd_target_elf_flavour)
	{
	  as_bad (_("-call_shared is supported only for ELF format"));
	  return 0;
	}
      mips_pic = SVR4_PIC;
      mips_abicalls = TRUE;
      if (g_switch_seen && g_switch_value != 0)
	{
	  as_bad (_("-G may not be used with SVR4 PIC code"));
	  return 0;
	}
      g_switch_value = 0;
      break;

    case OPTION_NON_SHARED:
      if (OUTPUT_FLAVOR != bfd_target_elf_flavour)
	{
	  as_bad (_("-non_shared is supported only for ELF format"));
	  return 0;
	}
      mips_pic = NO_PIC;
      mips_abicalls = FALSE;
      break;

      /* The -xgot option tells the assembler to use 32 offsets when
         accessing the got in SVR4_PIC mode.  It is for Irix
         compatibility.  */
    case OPTION_XGOT:
      mips_big_got = 1;
      break;
#endif /* OBJ_ELF */

    case 'G':
      if (! USE_GLOBAL_POINTER_OPT)
	{
	  as_bad (_("-G is not supported for this configuration"));
	  return 0;
	}
      else if (mips_pic == SVR4_PIC || mips_pic == EMBEDDED_PIC)
	{
	  as_bad (_("-G may not be used with SVR4 or embedded PIC code"));
	  return 0;
	}
      else
	g_switch_value = atoi (arg);
      g_switch_seen = 1;
      break;

#ifdef OBJ_ELF
      /* The -32, -n32 and -64 options are shortcuts for -mabi=32, -mabi=n32
	 and -mabi=64.  */
    case OPTION_32:
      if (OUTPUT_FLAVOR != bfd_target_elf_flavour)
	{
	  as_bad (_("-32 is supported for ELF format only"));
	  return 0;
	}
      mips_abi = O32_ABI;
      break;

    case OPTION_N32:
      if (OUTPUT_FLAVOR != bfd_target_elf_flavour)
	{
	  as_bad (_("-n32 is supported for ELF format only"));
	  return 0;
	}
      mips_abi = N32_ABI;
      break;

    case OPTION_64:
      if (OUTPUT_FLAVOR != bfd_target_elf_flavour)
	{
	  as_bad (_("-64 is supported for ELF format only"));
	  return 0;
	}
      mips_abi = N64_ABI;
      if (! support_64bit_objects())
	as_fatal (_("No compiled in support for 64 bit object file format"));
      break;
#endif /* OBJ_ELF */

    case OPTION_GP32:
      file_mips_gp32 = 1;
      break;

    case OPTION_GP64:
      file_mips_gp32 = 0;
      break;

    case OPTION_FP32:
      file_mips_fp32 = 1;
      break;

    case OPTION_FP64:
      file_mips_fp32 = 0;
      break;

#ifdef OBJ_ELF
    case OPTION_MABI:
      if (OUTPUT_FLAVOR != bfd_target_elf_flavour)
	{
	  as_bad (_("-mabi is supported for ELF format only"));
	  return 0;
	}
      if (strcmp (arg, "32") == 0)
	mips_abi = O32_ABI;
      else if (strcmp (arg, "o64") == 0)
	mips_abi = O64_ABI;
      else if (strcmp (arg, "n32") == 0)
	mips_abi = N32_ABI;
      else if (strcmp (arg, "64") == 0)
	{
	  mips_abi = N64_ABI;
	  if (! support_64bit_objects())
	    as_fatal (_("No compiled in support for 64 bit object file "
			"format"));
	}
      else if (strcmp (arg, "eabi") == 0)
	mips_abi = EABI_ABI;
      else
	{
	  as_fatal (_("invalid abi -mabi=%s"), arg);
	  return 0;
	}
      break;
#endif /* OBJ_ELF */

    case OPTION_M7000_HILO_FIX:
      mips_7000_hilo_fix = TRUE;
      break;

    case OPTION_MNO_7000_HILO_FIX:
      mips_7000_hilo_fix = FALSE;
      break;

#ifdef OBJ_ELF
    case OPTION_MDEBUG:
      mips_flag_mdebug = TRUE;
      break;

    case OPTION_NO_MDEBUG:
      mips_flag_mdebug = FALSE;
      break;

    case OPTION_PDR:
      mips_flag_pdr = TRUE;
      break;

    case OPTION_NO_PDR:
      mips_flag_pdr = FALSE;
      break;
#endif /* OBJ_ELF */

    default:
      return 0;
    }

  return 1;
}

/* Set up globals to generate code for the ISA or processor
   described by INFO.  */

static void
mips_set_architecture (const struct mips_cpu_info *info)
{
  if (info != 0)
    {
      file_mips_arch = info->cpu;
      mips_opts.arch = info->cpu;
      mips_opts.isa = info->isa;
    }
}


/* Likewise for tuning.  */

static void
mips_set_tune (const struct mips_cpu_info *info)
{
  if (info != 0)
    mips_tune = info->cpu;
}


void
mips_after_parse_args (void)
{
  const struct mips_cpu_info *arch_info = 0;
  const struct mips_cpu_info *tune_info = 0;

  /* GP relative stuff not working for PE */
  if (strncmp (TARGET_OS, "pe", 2) == 0
      && g_switch_value != 0)
    {
      if (g_switch_seen)
	as_bad (_("-G not supported in this configuration."));
      g_switch_value = 0;
    }

  if (mips_abi == NO_ABI)
    mips_abi = MIPS_DEFAULT_ABI;

  /* The following code determines the architecture and register size.
     Similar code was added to GCC 3.3 (see override_options() in
     config/mips/mips.c).  The GAS and GCC code should be kept in sync
     as much as possible.  */

  if (mips_arch_string != 0)
    arch_info = mips_parse_cpu ("-march", mips_arch_string);

  if (file_mips_isa != ISA_UNKNOWN)
    {
      /* Handle -mipsN.  At this point, file_mips_isa contains the
	 ISA level specified by -mipsN, while arch_info->isa contains
	 the -march selection (if any).  */
      if (arch_info != 0)
	{
	  /* -march takes precedence over -mipsN, since it is more descriptive.
	     There's no harm in specifying both as long as the ISA levels
	     are the same.  */
	  if (file_mips_isa != arch_info->isa)
	    as_bad (_("-%s conflicts with the other architecture options, which imply -%s"),
		    mips_cpu_info_from_isa (file_mips_isa)->name,
		    mips_cpu_info_from_isa (arch_info->isa)->name);
	}
      else
	arch_info = mips_cpu_info_from_isa (file_mips_isa);
    }

  if (arch_info == 0)
    arch_info = mips_parse_cpu ("default CPU", MIPS_CPU_STRING_DEFAULT);

  if (ABI_NEEDS_64BIT_REGS (mips_abi) && !ISA_HAS_64BIT_REGS (arch_info->isa))
    as_bad ("-march=%s is not compatible with the selected ABI",
	    arch_info->name);

  mips_set_architecture (arch_info);

  /* Optimize for file_mips_arch, unless -mtune selects a different processor.  */
  if (mips_tune_string != 0)
    tune_info = mips_parse_cpu ("-mtune", mips_tune_string);

  if (tune_info == 0)
    mips_set_tune (arch_info);
  else
    mips_set_tune (tune_info);

  if (file_mips_gp32 >= 0)
    {
      /* The user specified the size of the integer registers.  Make sure
	 it agrees with the ABI and ISA.  */
      if (file_mips_gp32 == 0 && !ISA_HAS_64BIT_REGS (mips_opts.isa))
	as_bad (_("-mgp64 used with a 32-bit processor"));
      else if (file_mips_gp32 == 1 && ABI_NEEDS_64BIT_REGS (mips_abi))
	as_bad (_("-mgp32 used with a 64-bit ABI"));
      else if (file_mips_gp32 == 0 && ABI_NEEDS_32BIT_REGS (mips_abi))
	as_bad (_("-mgp64 used with a 32-bit ABI"));
    }
  else
    {
      /* Infer the integer register size from the ABI and processor.
	 Restrict ourselves to 32-bit registers if that's all the
	 processor has, or if the ABI cannot handle 64-bit registers.  */
      file_mips_gp32 = (ABI_NEEDS_32BIT_REGS (mips_abi)
			|| !ISA_HAS_64BIT_REGS (mips_opts.isa));
    }

  /* ??? GAS treats single-float processors as though they had 64-bit
     float registers (although it complains when double-precision
     instructions are used).  As things stand, saying they have 32-bit
     registers would lead to spurious "register must be even" messages.
     So here we assume float registers are always the same size as
     integer ones, unless the user says otherwise.  */
  if (file_mips_fp32 < 0)
    file_mips_fp32 = file_mips_gp32;

  /* End of GCC-shared inference code.  */

  /* This flag is set when we have a 64-bit capable CPU but use only
     32-bit wide registers.  Note that EABI does not use it.  */
  if (ISA_HAS_64BIT_REGS (mips_opts.isa)
      && ((mips_abi == NO_ABI && file_mips_gp32 == 1)
	  || mips_abi == O32_ABI))
    mips_32bitmode = 1;

  if (mips_opts.isa == ISA_MIPS1 && mips_trap)
    as_bad (_("trap exception not supported at ISA 1"));

  /* If the selected architecture includes support for ASEs, enable
     generation of code for them.  */
  if (mips_opts.mips16 == -1)
    mips_opts.mips16 = (CPU_HAS_MIPS16 (file_mips_arch)) ? 1 : 0;
  if (mips_opts.ase_mips3d == -1)
    mips_opts.ase_mips3d = (CPU_HAS_MIPS3D (file_mips_arch)) ? 1 : 0;
  if (mips_opts.ase_mdmx == -1)
    mips_opts.ase_mdmx = (CPU_HAS_MDMX (file_mips_arch)) ? 1 : 0;

  file_mips_isa = mips_opts.isa;
  file_ase_mips16 = mips_opts.mips16;
  file_ase_mips3d = mips_opts.ase_mips3d;
  file_ase_mdmx = mips_opts.ase_mdmx;
  mips_opts.gp32 = file_mips_gp32;
  mips_opts.fp32 = file_mips_fp32;

  if (mips_flag_mdebug < 0)
    {
#ifdef OBJ_MAYBE_ECOFF
      if (OUTPUT_FLAVOR == bfd_target_ecoff_flavour)
	mips_flag_mdebug = 1;
      else
#endif /* OBJ_MAYBE_ECOFF */
	mips_flag_mdebug = 0;
    }
}

void
mips_init_after_args (void)
{
  /* initialize opcodes */
  bfd_mips_num_opcodes = bfd_mips_num_builtin_opcodes;
  mips_opcodes = (struct mips_opcode *) mips_builtin_opcodes;
}

long
md_pcrel_from (fixS *fixP)
{
  valueT addr = fixP->fx_where + fixP->fx_frag->fr_address;
  switch (fixP->fx_r_type)
    {
    case BFD_RELOC_16_PCREL_S2:
    case BFD_RELOC_MIPS_JMP:
      /* Return the address of the delay slot.  */
      return addr + 4;
    default:
      return addr;
    }
}

/* This is called before the symbol table is processed.  In order to
   work with gcc when using mips-tfile, we must keep all local labels.
   However, in other cases, we want to discard them.  If we were
   called with -g, but we didn't see any debugging information, it may
   mean that gcc is smuggling debugging information through to
   mips-tfile, in which case we must generate all local labels.  */

void
mips_frob_file_before_adjust (void)
{
#ifndef NO_ECOFF_DEBUGGING
  if (ECOFF_DEBUGGING
      && mips_debug != 0
      && ! ecoff_debugging_seen)
    flag_keep_locals = 1;
#endif
}

/* Sort any unmatched HI16_S relocs so that they immediately precede
   the corresponding LO reloc.  This is called before md_apply_fix3 and
   tc_gen_reloc.  Unmatched HI16_S relocs can only be generated by
   explicit use of the %hi modifier.  */

void
mips_frob_file (void)
{
  struct mips_hi_fixup *l;

  for (l = mips_hi_fixup_list; l != NULL; l = l->next)
    {
      segment_info_type *seginfo;
      int pass;

      assert (reloc_needs_lo_p (l->fixp->fx_r_type));

      /* If a GOT16 relocation turns out to be against a global symbol,
	 there isn't supposed to be a matching LO.  */
      if (l->fixp->fx_r_type == BFD_RELOC_MIPS_GOT16
	  && !pic_need_relax (l->fixp->fx_addsy, l->seg))
	continue;

      /* Check quickly whether the next fixup happens to be a matching %lo.  */
      if (fixup_has_matching_lo_p (l->fixp))
	continue;

      /* Look through the fixups for this segment for a matching %lo.
         When we find one, move the %hi just in front of it.  We do
         this in two passes.  In the first pass, we try to find a
         unique %lo.  In the second pass, we permit multiple %hi
         relocs for a single %lo (this is a GNU extension).  */
      seginfo = seg_info (l->seg);
      for (pass = 0; pass < 2; pass++)
	{
	  fixS *f, *prev;

	  prev = NULL;
	  for (f = seginfo->fix_root; f != NULL; f = f->fx_next)
	    {
	      /* Check whether this is a %lo fixup which matches l->fixp.  */
	      if (f->fx_r_type == BFD_RELOC_LO16
		  && f->fx_addsy == l->fixp->fx_addsy
		  && f->fx_offset == l->fixp->fx_offset
		  && (pass == 1
		      || prev == NULL
		      || !reloc_needs_lo_p (prev->fx_r_type)
		      || !fixup_has_matching_lo_p (prev)))
		{
		  fixS **pf;

		  /* Move l->fixp before f.  */
		  for (pf = &seginfo->fix_root;
		       *pf != l->fixp;
		       pf = &(*pf)->fx_next)
		    assert (*pf != NULL);

		  *pf = l->fixp->fx_next;

		  l->fixp->fx_next = f;
		  if (prev == NULL)
		    seginfo->fix_root = l->fixp;
		  else
		    prev->fx_next = l->fixp;

		  break;
		}

	      prev = f;
	    }

	  if (f != NULL)
	    break;

#if 0 /* GCC code motion plus incomplete dead code elimination
	 can leave a %hi without a %lo.  */
	  if (pass == 1)
	    as_warn_where (l->fixp->fx_file, l->fixp->fx_line,
			   _("Unmatched %%hi reloc"));
#endif
	}
    }
}

/* When generating embedded PIC code we need to use a special
   relocation to represent the difference of two symbols in the .text
   section (switch tables use a difference of this sort).  See
   include/coff/mips.h for details.  This macro checks whether this
   fixup requires the special reloc.  */
#define SWITCH_TABLE(fixp) \
  ((fixp)->fx_r_type == BFD_RELOC_32 \
   && OUTPUT_FLAVOR != bfd_target_elf_flavour \
   && (fixp)->fx_addsy != NULL \
   && (fixp)->fx_subsy != NULL \
   && S_GET_SEGMENT ((fixp)->fx_addsy) == text_section \
   && S_GET_SEGMENT ((fixp)->fx_subsy) == text_section)

/* When generating embedded PIC code we must keep all PC relative
   relocations, in case the linker has to relax a call.  We also need
   to keep relocations for switch table entries.

   We may have combined relocations without symbols in the N32/N64 ABI.
   We have to prevent gas from dropping them.  */

int
mips_force_relocation (fixS *fixp)
{
  if (generic_force_reloc (fixp))
    return 1;

  if (HAVE_NEWABI
      && S_GET_SEGMENT (fixp->fx_addsy) == bfd_abs_section_ptr
      && (fixp->fx_r_type == BFD_RELOC_MIPS_SUB
	  || fixp->fx_r_type == BFD_RELOC_HI16_S
	  || fixp->fx_r_type == BFD_RELOC_LO16))
    return 1;

  return (mips_pic == EMBEDDED_PIC
	  && (fixp->fx_pcrel
	      || SWITCH_TABLE (fixp)
	      || fixp->fx_r_type == BFD_RELOC_PCREL_HI16_S
	      || fixp->fx_r_type == BFD_RELOC_PCREL_LO16));
}

/* This hook is called before a fix is simplified.  We don't really
   decide whether to skip a fix here.  Rather, we turn global symbols
   used as branch targets into local symbols, such that they undergo
   simplification.  We can only do this if the symbol is defined and
   it is in the same section as the branch.  If this doesn't hold, we
   emit a better error message than just saying the relocation is not
   valid for the selected object format.

   FIXP is the fix-up we're going to try to simplify, SEG is the
   segment in which the fix up occurs.  The return value should be
   non-zero to indicate the fix-up is valid for further
   simplifications.  */

int
mips_validate_fix (struct fix *fixP, asection *seg)
{
  /* There's a lot of discussion on whether it should be possible to
     use R_MIPS_PC16 to represent branch relocations.  The outcome
     seems to be that it can, but gas/bfd are very broken in creating
     RELA relocations for this, so for now we only accept branches to
     symbols in the same section.  Anything else is of dubious value,
     since there's no guarantee that at link time the symbol would be
     in range.  Even for branches to local symbols this is arguably
     wrong, since it we assume the symbol is not going to be
     overridden, which should be possible per ELF library semantics,
     but then, there isn't a dynamic relocation that could be used to
     this effect, and the target would likely be out of range as well.

     Unfortunately, it seems that there is too much code out there
     that relies on branches to symbols that are global to be resolved
     as if they were local, like the IRIX tools do, so we do it as
     well, but with a warning so that people are reminded to fix their
     code.  If we ever get back to using R_MIPS_PC16 for branch
     targets, this entire block should go away (and probably the
     whole function).  */

  if (fixP->fx_r_type == BFD_RELOC_16_PCREL_S2
      && (((OUTPUT_FLAVOR == bfd_target_ecoff_flavour
	    || OUTPUT_FLAVOR == bfd_target_elf_flavour)
	   && mips_pic != EMBEDDED_PIC)
	  || bfd_reloc_type_lookup (stdoutput, BFD_RELOC_16_PCREL_S2) == NULL)
      && fixP->fx_addsy)
    {
      if (! S_IS_DEFINED (fixP->fx_addsy))
	{
	  as_bad_where (fixP->fx_file, fixP->fx_line,
			_("Cannot branch to undefined symbol."));
	  /* Avoid any further errors about this fixup.  */
	  fixP->fx_done = 1;
	}
      else if (S_GET_SEGMENT (fixP->fx_addsy) != seg)
	{
	  as_bad_where (fixP->fx_file, fixP->fx_line,
			_("Cannot branch to symbol in another section."));
	  fixP->fx_done = 1;
	}
      else if (S_IS_EXTERNAL (fixP->fx_addsy))
	{
	  symbolS *sym = fixP->fx_addsy;

	  if (mips_pic == SVR4_PIC)
	    as_warn_where (fixP->fx_file, fixP->fx_line,
			   _("Pretending global symbol used as branch target is local."));

	  fixP->fx_addsy = symbol_create (S_GET_NAME (sym),
					  S_GET_SEGMENT (sym),
					  S_GET_VALUE (sym),
					  symbol_get_frag (sym));
	  copy_symbol_attributes (fixP->fx_addsy, sym);
	  S_CLEAR_EXTERNAL (fixP->fx_addsy);
	  assert (symbol_resolved_p (sym));
	  symbol_mark_resolved (fixP->fx_addsy);
	}
    }

  return 1;
}

/* Apply a fixup to the object file.  */

void
md_apply_fix3 (fixS *fixP, valueT *valP, segT seg ATTRIBUTE_UNUSED)
{
  bfd_byte *buf;
  long insn;
  static int previous_fx_r_type = 0;
  reloc_howto_type *howto;

  /* We ignore generic BFD relocations we don't know about.  */
  howto = bfd_reloc_type_lookup (stdoutput, fixP->fx_r_type);
  if (! howto)
    return;

  assert (fixP->fx_size == 4
	  || fixP->fx_r_type == BFD_RELOC_16
	  || fixP->fx_r_type == BFD_RELOC_64
	  || fixP->fx_r_type == BFD_RELOC_CTOR
	  || fixP->fx_r_type == BFD_RELOC_MIPS_SUB
	  || fixP->fx_r_type == BFD_RELOC_VTABLE_INHERIT
	  || fixP->fx_r_type == BFD_RELOC_VTABLE_ENTRY);

  buf = (bfd_byte *) (fixP->fx_frag->fr_literal + fixP->fx_where);

  /* We are not done if this is a composite relocation to set up gp.  */
  if (fixP->fx_addsy == NULL && ! fixP->fx_pcrel
      && !(fixP->fx_r_type == BFD_RELOC_MIPS_SUB
	   || (fixP->fx_r_type == BFD_RELOC_64
	       && (previous_fx_r_type == BFD_RELOC_GPREL32
		   || previous_fx_r_type == BFD_RELOC_GPREL16))
	   || (previous_fx_r_type == BFD_RELOC_MIPS_SUB
	       && (fixP->fx_r_type == BFD_RELOC_HI16_S
		   || fixP->fx_r_type == BFD_RELOC_LO16))))
    fixP->fx_done = 1;
  previous_fx_r_type = fixP->fx_r_type;

  switch (fixP->fx_r_type)
    {
    case BFD_RELOC_MIPS_JMP:
    case BFD_RELOC_MIPS_SHIFT5:
    case BFD_RELOC_MIPS_SHIFT6:
    case BFD_RELOC_MIPS_GOT_DISP:
    case BFD_RELOC_MIPS_GOT_PAGE:
    case BFD_RELOC_MIPS_GOT_OFST:
    case BFD_RELOC_MIPS_SUB:
    case BFD_RELOC_MIPS_INSERT_A:
    case BFD_RELOC_MIPS_INSERT_B:
    case BFD_RELOC_MIPS_DELETE:
    case BFD_RELOC_MIPS_HIGHEST:
    case BFD_RELOC_MIPS_HIGHER:
    case BFD_RELOC_MIPS_SCN_DISP:
    case BFD_RELOC_MIPS_REL16:
    case BFD_RELOC_MIPS_RELGOT:
    case BFD_RELOC_MIPS_JALR:
    case BFD_RELOC_HI16:
    case BFD_RELOC_HI16_S:
    case BFD_RELOC_GPREL16:
    case BFD_RELOC_MIPS_LITERAL:
    case BFD_RELOC_MIPS_CALL16:
    case BFD_RELOC_MIPS_GOT16:
    case BFD_RELOC_GPREL32:
    case BFD_RELOC_MIPS_GOT_HI16:
    case BFD_RELOC_MIPS_GOT_LO16:
    case BFD_RELOC_MIPS_CALL_HI16:
    case BFD_RELOC_MIPS_CALL_LO16:
    case BFD_RELOC_MIPS16_GPREL:
      if (fixP->fx_pcrel)
	as_bad_where (fixP->fx_file, fixP->fx_line,
		      _("Invalid PC relative reloc"));
      /* Nothing needed to do. The value comes from the reloc entry */
      break;

    case BFD_RELOC_MIPS16_JMP:
      /* We currently always generate a reloc against a symbol, which
         means that we don't want an addend even if the symbol is
         defined.  */
      *valP = 0;
      break;

    case BFD_RELOC_PCREL_HI16_S:
      /* The addend for this is tricky if it is internal, so we just
	 do everything here rather than in bfd_install_relocation.  */
      if (OUTPUT_FLAVOR == bfd_target_elf_flavour && !fixP->fx_done)
	break;
      if (fixP->fx_addsy
	  && (symbol_get_bfdsym (fixP->fx_addsy)->flags & BSF_SECTION_SYM) == 0)
	{
	  /* For an external symbol adjust by the address to make it
	     pcrel_offset.  We use the address of the RELLO reloc
	     which follows this one.  */
	  *valP += (fixP->fx_next->fx_frag->fr_address
		    + fixP->fx_next->fx_where);
	}
      *valP = ((*valP + 0x8000) >> 16) & 0xffff;
      if (target_big_endian)
	buf += 2;
      md_number_to_chars (buf, *valP, 2);
      break;

    case BFD_RELOC_PCREL_LO16:
      /* The addend for this is tricky if it is internal, so we just
	 do everything here rather than in bfd_install_relocation.  */
      if (OUTPUT_FLAVOR == bfd_target_elf_flavour && !fixP->fx_done)
	break;
      if (fixP->fx_addsy
	  && (symbol_get_bfdsym (fixP->fx_addsy)->flags & BSF_SECTION_SYM) == 0)
	*valP += fixP->fx_frag->fr_address + fixP->fx_where;
      if (target_big_endian)
	buf += 2;
      md_number_to_chars (buf, *valP, 2);
      break;

    case BFD_RELOC_64:
      /* This is handled like BFD_RELOC_32, but we output a sign
         extended value if we are only 32 bits.  */
      if (fixP->fx_done
	  || (mips_pic == EMBEDDED_PIC && SWITCH_TABLE (fixP)))
	{
	  if (8 <= sizeof (valueT))
	    md_number_to_chars (buf, *valP, 8);
	  else
	    {
	      valueT hiv;

	      if ((*valP & 0x80000000) != 0)
		hiv = 0xffffffff;
	      else
		hiv = 0;
	      md_number_to_chars ((char *)(buf + target_big_endian ? 4 : 0),
				  *valP, 4);
	      md_number_to_chars ((char *)(buf + target_big_endian ? 0 : 4),
				  hiv, 4);
	    }
	}
      break;

    case BFD_RELOC_RVA:
    case BFD_RELOC_32:
      /* If we are deleting this reloc entry, we must fill in the
	 value now.  This can happen if we have a .word which is not
	 resolved when it appears but is later defined.  We also need
	 to fill in the value if this is an embedded PIC switch table
	 entry.  */
      if (fixP->fx_done
	  || (mips_pic == EMBEDDED_PIC && SWITCH_TABLE (fixP)))
	md_number_to_chars (buf, *valP, 4);
      break;

    case BFD_RELOC_16:
      /* If we are deleting this reloc entry, we must fill in the
         value now.  */
      assert (fixP->fx_size == 2);
      if (fixP->fx_done)
	md_number_to_chars (buf, *valP, 2);
      break;

    case BFD_RELOC_LO16:
      /* When handling an embedded PIC switch statement, we can wind
	 up deleting a LO16 reloc.  See the 'o' case in mips_ip.  */
      if (fixP->fx_done)
	{
	  if (*valP + 0x8000 > 0xffff)
	    as_bad_where (fixP->fx_file, fixP->fx_line,
			  _("relocation overflow"));
	  if (target_big_endian)
	    buf += 2;
	  md_number_to_chars (buf, *valP, 2);
	}
      break;

    case BFD_RELOC_16_PCREL_S2:
      if ((*valP & 0x3) != 0)
	as_bad_where (fixP->fx_file, fixP->fx_line,
		      _("Branch to odd address (%lx)"), (long) *valP);

      /*
       * We need to save the bits in the instruction since fixup_segment()
       * might be deleting the relocation entry (i.e., a branch within
       * the current segment).
       */
      if (! fixP->fx_done)
	break;

      /* update old instruction data */
      if (target_big_endian)
	insn = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
      else
	insn = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];

      if (*valP + 0x20000 <= 0x3ffff)
	{
	  insn |= (*valP >> 2) & 0xffff;
	  md_number_to_chars (buf, insn, 4);
	}
      else if (mips_pic == NO_PIC
	       && fixP->fx_done
	       && fixP->fx_frag->fr_address >= text_section->vma
	       && (fixP->fx_frag->fr_address
		   < text_section->vma + text_section->_raw_size)
	       && ((insn & 0xffff0000) == 0x10000000	 /* beq $0,$0 */
		   || (insn & 0xffff0000) == 0x04010000	 /* bgez $0 */
		   || (insn & 0xffff0000) == 0x04110000)) /* bgezal $0 */
	{
	  /* The branch offset is too large.  If this is an
             unconditional branch, and we are not generating PIC code,
             we can convert it to an absolute jump instruction.  */
	  if ((insn & 0xffff0000) == 0x04110000)	 /* bgezal $0 */
	    insn = 0x0c000000;	/* jal */
	  else
	    insn = 0x08000000;	/* j */
	  fixP->fx_r_type = BFD_RELOC_MIPS_JMP;
	  fixP->fx_done = 0;
	  fixP->fx_addsy = section_symbol (text_section);
	  *valP += md_pcrel_from (fixP);
	  md_number_to_chars (buf, insn, 4);
	}
      else
	{
	  /* If we got here, we have branch-relaxation disabled,
	     and there's nothing we can do to fix this instruction
	     without turning it into a longer sequence.  */
	  as_bad_where (fixP->fx_file, fixP->fx_line,
			_("Branch out of range"));
	}
      break;

    case BFD_RELOC_VTABLE_INHERIT:
      fixP->fx_done = 0;
      if (fixP->fx_addsy
          && !S_IS_DEFINED (fixP->fx_addsy)
          && !S_IS_WEAK (fixP->fx_addsy))
        S_SET_WEAK (fixP->fx_addsy);
      break;

    case BFD_RELOC_VTABLE_ENTRY:
      fixP->fx_done = 0;
      break;

    default:
      internalError ();
    }

  /* Remember value for tc_gen_reloc.  */
  fixP->fx_addnumber = *valP;
}

#if 0
void
printInsn (unsigned long oc)
{
  const struct mips_opcode *p;
  int treg, sreg, dreg, shamt;
  short imm;
  const char *args;
  int i;

  for (i = 0; i < NUMOPCODES; ++i)
    {
      p = &mips_opcodes[i];
      if (((oc & p->mask) == p->match) && (p->pinfo != INSN_MACRO))
	{
	  printf ("%08lx %s\t", oc, p->name);
	  treg = (oc >> 16) & 0x1f;
	  sreg = (oc >> 21) & 0x1f;
	  dreg = (oc >> 11) & 0x1f;
	  shamt = (oc >> 6) & 0x1f;
	  imm = oc;
	  for (args = p->args;; ++args)
	    {
	      switch (*args)
		{
		case '\0':
		  printf ("\n");
		  break;

		case ',':
		case '(':
		case ')':
		  printf ("%c", *args);
		  continue;

		case 'r':
		  assert (treg == sreg);
		  printf ("$%d,$%d", treg, sreg);
		  continue;

		case 'd':
		case 'G':
		  printf ("$%d", dreg);
		  continue;

		case 't':
		case 'E':
		  printf ("$%d", treg);
		  continue;

		case 'k':
		  printf ("0x%x", treg);
		  continue;

		case 'b':
		case 's':
		  printf ("$%d", sreg);
		  continue;

		case 'a':
		  printf ("0x%08lx", oc & 0x1ffffff);
		  continue;

		case 'i':
		case 'j':
		case 'o':
		case 'u':
		  printf ("%d", imm);
		  continue;

		case '<':
		case '>':
		  printf ("$%d", shamt);
		  continue;

		default:
		  internalError ();
		}
	      break;
	    }
	  return;
	}
    }
  printf (_("%08lx  UNDEFINED\n"), oc);
}
#endif

static symbolS *
get_symbol (void)
{
  int c;
  char *name;
  symbolS *p;

  name = input_line_pointer;
  c = get_symbol_end ();
  p = (symbolS *) symbol_find_or_make (name);
  *input_line_pointer = c;
  return p;
}

/* Align the current frag to a given power of two.  The MIPS assembler
   also automatically adjusts any preceding label.  */

static void
mips_align (int to, int fill, symbolS *label)
{
  mips_emit_delays (FALSE);
  frag_align (to, fill, 0);
  record_alignment (now_seg, to);
  if (label != NULL)
    {
      assert (S_GET_SEGMENT (label) == now_seg);
      symbol_set_frag (label, frag_now);
      S_SET_VALUE (label, (valueT) frag_now_fix ());
    }
}

/* Align to a given power of two.  .align 0 turns off the automatic
   alignment used by the data creating pseudo-ops.  */

static void
s_align (int x ATTRIBUTE_UNUSED)
{
  register int temp;
  register long temp_fill;
  long max_alignment = 15;

  /*

    o  Note that the assembler pulls down any immediately preceding label
       to the aligned address.
    o  It's not documented but auto alignment is reinstated by
       a .align pseudo instruction.
    o  Note also that after auto alignment is turned off the mips assembler
       issues an error on attempt to assemble an improperly aligned data item.
       We don't.

    */

  temp = get_absolute_expression ();
  if (temp > max_alignment)
    as_bad (_("Alignment too large: %d. assumed."), temp = max_alignment);
  else if (temp < 0)
    {
      as_warn (_("Alignment negative: 0 assumed."));
      temp = 0;
    }
  if (*input_line_pointer == ',')
    {
      ++input_line_pointer;
      temp_fill = get_absolute_expression ();
    }
  else
    temp_fill = 0;
  if (temp)
    {
      auto_align = 1;
      mips_align (temp, (int) temp_fill,
		  insn_labels != NULL ? insn_labels->label : NULL);
    }
  else
    {
      auto_align = 0;
    }

  demand_empty_rest_of_line ();
}

void
mips_flush_pending_output (void)
{
  mips_emit_delays (FALSE);
  mips_clear_insn_labels ();
}

static void
s_change_sec (int sec)
{
  segT seg;

  /* When generating embedded PIC code, we only use the .text, .lit8,
     .sdata and .sbss sections.  We change the .data and .rdata
     pseudo-ops to use .sdata.  */
  if (mips_pic == EMBEDDED_PIC
      && (sec == 'd' || sec == 'r'))
    sec = 's';

#ifdef OBJ_ELF
  /* The ELF backend needs to know that we are changing sections, so
     that .previous works correctly.  We could do something like check
     for an obj_section_change_hook macro, but that might be confusing
     as it would not be appropriate to use it in the section changing
     functions in read.c, since obj-elf.c intercepts those.  FIXME:
     This should be cleaner, somehow.  */
  obj_elf_section_change_hook ();
#endif

  mips_emit_delays (FALSE);
  switch (sec)
    {
    case 't':
      s_text (0);
      break;
    case 'd':
      s_data (0);
      break;
    case 'b':
      subseg_set (bss_section, (subsegT) get_absolute_expression ());
      demand_empty_rest_of_line ();
      break;

    case 'r':
      if (USE_GLOBAL_POINTER_OPT)
	{
	  seg = subseg_new (RDATA_SECTION_NAME,
			    (subsegT) get_absolute_expression ());
	  if (OUTPUT_FLAVOR == bfd_target_elf_flavour)
	    {
	      bfd_set_section_flags (stdoutput, seg,
				     (SEC_ALLOC
				      | SEC_LOAD
				      | SEC_READONLY
				      | SEC_RELOC
				      | SEC_DATA));
	      if (strcmp (TARGET_OS, "elf") != 0)
		record_alignment (seg, 4);
	    }
	  demand_empty_rest_of_line ();
	}
      else
	{
	  as_bad (_("No read only data section in this object file format"));
	  demand_empty_rest_of_line ();
	  return;
	}
      break;

    case 's':
      if (USE_GLOBAL_POINTER_OPT)
	{
	  seg = subseg_new (".sdata", (subsegT) get_absolute_expression ());
	  if (OUTPUT_FLAVOR == bfd_target_elf_flavour)
	    {
	      bfd_set_section_flags (stdoutput, seg,
				     SEC_ALLOC | SEC_LOAD | SEC_RELOC
				     | SEC_DATA);
	      if (strcmp (TARGET_OS, "elf") != 0)
		record_alignment (seg, 4);
	    }
	  demand_empty_rest_of_line ();
	  break;
	}
      else
	{
	  as_bad (_("Global pointers not supported; recompile -G 0"));
	  demand_empty_rest_of_line ();
	  return;
	}
    }

  auto_align = 1;
}

void
s_change_section (int ignore ATTRIBUTE_UNUSED)
{
#ifdef OBJ_ELF
  char *section_name;
  char c;
  char next_c = 0;
  int section_type;
  int section_flag;
  int section_entry_size;
  int section_alignment;

  if (OUTPUT_FLAVOR != bfd_target_elf_flavour)
    return;

  section_name = input_line_pointer;
  c = get_symbol_end ();
  if (c)
    next_c = *(input_line_pointer + 1);

  /* Do we have .section Name<,"flags">?  */
  if (c != ',' || (c == ',' && next_c == '"'))
    {
      /* just after name is now '\0'.  */
      *input_line_pointer = c;
      input_line_pointer = section_name;
      obj_elf_section (ignore);
      return;
    }
  input_line_pointer++;

  /* Do we have .section Name<,type><,flag><,entry_size><,alignment>  */
  if (c == ',')
    section_type = get_absolute_expression ();
  else
    section_type = 0;
  if (*input_line_pointer++ == ',')
    section_flag = get_absolute_expression ();
  else
    section_flag = 0;
  if (*input_line_pointer++ == ',')
    section_entry_size = get_absolute_expression ();
  else
    section_entry_size = 0;
  if (*input_line_pointer++ == ',')
    section_alignment = get_absolute_expression ();
  else
    section_alignment = 0;

  section_name = xstrdup (section_name);

  /* When using the generic form of .section (as implemented by obj-elf.c),
     there's no way to set the section type to SHT_MIPS_DWARF.  Users have
     traditionally had to fall back on the more common @progbits instead.

     There's nothing really harmful in this, since bfd will correct
     SHT_PROGBITS to SHT_MIPS_DWARF before writing out the file.  But it
     means that, for backwards compatibiltiy, the special_section entries
     for dwarf sections must use SHT_PROGBITS rather than SHT_MIPS_DWARF.

     Even so, we shouldn't force users of the MIPS .section syntax to
     incorrectly label the sections as SHT_PROGBITS.  The best compromise
     seems to be to map SHT_MIPS_DWARF to SHT_PROGBITS before calling the
     generic type-checking code.  */
  if (section_type == SHT_MIPS_DWARF)
    section_type = SHT_PROGBITS;

  obj_elf_change_section (section_name, section_type, section_flag,
			  section_entry_size, 0, 0, 0);

  if (now_seg->name != section_name)
    free (section_name);
#endif /* OBJ_ELF */
}

void
mips_enable_auto_align (void)
{
  auto_align = 1;
}

static void
s_cons (int log_size)
{
  symbolS *label;

  label = insn_labels != NULL ? insn_labels->label : NULL;
  mips_emit_delays (FALSE);
  if (log_size > 0 && auto_align)
    mips_align (log_size, 0, label);
  mips_clear_insn_labels ();
  cons (1 << log_size);
}

static void
s_float_cons (int type)
{
  symbolS *label;

  label = insn_labels != NULL ? insn_labels->label : NULL;

  mips_emit_delays (FALSE);

  if (auto_align)
    {
      if (type == 'd')
	mips_align (3, 0, label);
      else
	mips_align (2, 0, label);
    }

  mips_clear_insn_labels ();

  float_cons (type);
}

/* Handle .globl.  We need to override it because on Irix 5 you are
   permitted to say
       .globl foo .text
   where foo is an undefined symbol, to mean that foo should be
   considered to be the address of a function.  */

static void
s_mips_globl (int x ATTRIBUTE_UNUSED)
{
  char *name;
  int c;
  symbolS *symbolP;
  flagword flag;

  name = input_line_pointer;
  c = get_symbol_end ();
  symbolP = symbol_find_or_make (name);
  *input_line_pointer = c;
  SKIP_WHITESPACE ();

  /* On Irix 5, every global symbol that is not explicitly labelled as
     being a function is apparently labelled as being an object.  */
  flag = BSF_OBJECT;

  if (! is_end_of_line[(unsigned char) *input_line_pointer])
    {
      char *secname;
      asection *sec;

      secname = input_line_pointer;
      c = get_symbol_end ();
      sec = bfd_get_section_by_name (stdoutput, secname);
      if (sec == NULL)
	as_bad (_("%s: no such section"), secname);
      *input_line_pointer = c;

      if (sec != NULL && (sec->flags & SEC_CODE) != 0)
	flag = BSF_FUNCTION;
    }

  symbol_get_bfdsym (symbolP)->flags |= flag;

  S_SET_EXTERNAL (symbolP);
  demand_empty_rest_of_line ();
}

static void
s_option (int x ATTRIBUTE_UNUSED)
{
  char *opt;
  char c;

  opt = input_line_pointer;
  c = get_symbol_end ();

  if (*opt == 'O')
    {
      /* FIXME: What does this mean?  */
    }
  else if (strncmp (opt, "pic", 3) == 0)
    {
      int i;

      i = atoi (opt + 3);
      if (i == 0)
	mips_pic = NO_PIC;
      else if (i == 2)
	{
	mips_pic = SVR4_PIC;
	  mips_abicalls = TRUE;
	}
      else
	as_bad (_(".option pic%d not supported"), i);

      if (USE_GLOBAL_POINTER_OPT && mips_pic == SVR4_PIC)
	{
	  if (g_switch_seen && g_switch_value != 0)
	    as_warn (_("-G may not be used with SVR4 PIC code"));
	  g_switch_value = 0;
	  bfd_set_gp_size (stdoutput, 0);
	}
    }
  else
    as_warn (_("Unrecognized option \"%s\""), opt);

  *input_line_pointer = c;
  demand_empty_rest_of_line ();
}

/* This structure is used to hold a stack of .set values.  */

struct mips_option_stack
{
  struct mips_option_stack *next;
  struct mips_set_options options;
};

static struct mips_option_stack *mips_opts_stack;

/* Handle the .set pseudo-op.  */

static void
s_mipsset (int x ATTRIBUTE_UNUSED)
{
  char *name = input_line_pointer, ch;

  while (!is_end_of_line[(unsigned char) *input_line_pointer])
    ++input_line_pointer;
  ch = *input_line_pointer;
  *input_line_pointer = '\0';

  if (strcmp (name, "reorder") == 0)
    {
      if (mips_opts.noreorder && prev_nop_frag != NULL)
	{
	  /* If we still have pending nops, we can discard them.  The
	     usual nop handling will insert any that are still
	     needed.  */
	  prev_nop_frag->fr_fix -= (prev_nop_frag_holds
				    * (mips_opts.mips16 ? 2 : 4));
	  prev_nop_frag = NULL;
	}
      mips_opts.noreorder = 0;
    }
  else if (strcmp (name, "noreorder") == 0)
    {
      mips_emit_delays (TRUE);
      mips_opts.noreorder = 1;
      mips_any_noreorder = 1;
    }
  else if (strcmp (name, "at") == 0)
    {
      mips_opts.noat = 0;
    }
  else if (strcmp (name, "noat") == 0)
    {
      mips_opts.noat = 1;
    }
  else if (strcmp (name, "macro") == 0)
    {
      mips_opts.warn_about_macros = 0;
    }
  else if (strcmp (name, "nomacro") == 0)
    {
      if (mips_opts.noreorder == 0)
	as_bad (_("`noreorder' must be set before `nomacro'"));
      mips_opts.warn_about_macros = 1;
    }
  else if (strcmp (name, "move") == 0 || strcmp (name, "novolatile") == 0)
    {
      mips_opts.nomove = 0;
    }
  else if (strcmp (name, "nomove") == 0 || strcmp (name, "volatile") == 0)
    {
      mips_opts.nomove = 1;
    }
  else if (strcmp (name, "bopt") == 0)
    {
      mips_opts.nobopt = 0;
    }
  else if (strcmp (name, "nobopt") == 0)
    {
      mips_opts.nobopt = 1;
    }
  else if (strcmp (name, "mips16") == 0
	   || strcmp (name, "MIPS-16") == 0)
    mips_opts.mips16 = 1;
  else if (strcmp (name, "nomips16") == 0
	   || strcmp (name, "noMIPS-16") == 0)
    mips_opts.mips16 = 0;
  else if (strcmp (name, "mips3d") == 0)
    mips_opts.ase_mips3d = 1;
  else if (strcmp (name, "nomips3d") == 0)
    mips_opts.ase_mips3d = 0;
  else if (strcmp (name, "mdmx") == 0)
    mips_opts.ase_mdmx = 1;
  else if (strcmp (name, "nomdmx") == 0)
    mips_opts.ase_mdmx = 0;
  else if (strncmp (name, "mips", 4) == 0 || strncmp (name, "arch=", 5) == 0)
    {
      int reset = 0;

      /* Permit the user to change the ISA and architecture on the fly.
	 Needless to say, misuse can cause serious problems.  */
      if (strcmp (name, "mips0") == 0)
	{
	  reset = 1;
	  mips_opts.isa = file_mips_isa;
	}
      else if (strcmp (name, "mips1") == 0)
	mips_opts.isa = ISA_MIPS1;
      else if (strcmp (name, "mips2") == 0)
	mips_opts.isa = ISA_MIPS2;
      else if (strcmp (name, "mips3") == 0)
	mips_opts.isa = ISA_MIPS3;
      else if (strcmp (name, "mips4") == 0)
	mips_opts.isa = ISA_MIPS4;
      else if (strcmp (name, "mips5") == 0)
	mips_opts.isa = ISA_MIPS5;
      else if (strcmp (name, "mips32") == 0)
	mips_opts.isa = ISA_MIPS32;
      else if (strcmp (name, "mips32r2") == 0)
	mips_opts.isa = ISA_MIPS32R2;
      else if (strcmp (name, "mips64") == 0)
	mips_opts.isa = ISA_MIPS64;
      else if (strcmp (name, "mips64r2") == 0)
	mips_opts.isa = ISA_MIPS64R2;
      else if (strcmp (name, "arch=default") == 0)
	{
	  reset = 1;
	  mips_opts.arch = file_mips_arch;
	  mips_opts.isa = file_mips_isa;
	}
      else if (strncmp (name, "arch=", 5) == 0)
	{
	  const struct mips_cpu_info *p;

	  p = mips_parse_cpu("internal use", name + 5);
	  if (!p)
	    as_bad (_("unknown architecture %s"), name + 5);
	  else
	    {
	      mips_opts.arch = p->cpu;
	      mips_opts.isa = p->isa;
	    }
	}
      else
	as_bad (_("unknown ISA level %s"), name + 4);

      switch (mips_opts.isa)
	{
	case  0:
	  break;
	case ISA_MIPS1:
	case ISA_MIPS2:
	case ISA_MIPS32:
	case ISA_MIPS32R2:
	  mips_opts.gp32 = 1;
	  mips_opts.fp32 = 1;
	  break;
	case ISA_MIPS3:
	case ISA_MIPS4:
	case ISA_MIPS5:
	case ISA_MIPS64:
	case ISA_MIPS64R2:
	  mips_opts.gp32 = 0;
	  mips_opts.fp32 = 0;
	  break;
	default:
	  as_bad (_("unknown ISA level %s"), name + 4);
	  break;
	}
      if (reset)
	{
	  mips_opts.gp32 = file_mips_gp32;
	  mips_opts.fp32 = file_mips_fp32;
	}
    }
  else if (strcmp (name, "autoextend") == 0)
    mips_opts.noautoextend = 0;
  else if (strcmp (name, "noautoextend") == 0)
    mips_opts.noautoextend = 1;
  else if (strcmp (name, "push") == 0)
    {
      struct mips_option_stack *s;

      s = (struct mips_option_stack *) xmalloc (sizeof *s);
      s->next = mips_opts_stack;
      s->options = mips_opts;
      mips_opts_stack = s;
    }
  else if (strcmp (name, "pop") == 0)
    {
      struct mips_option_stack *s;

      s = mips_opts_stack;
      if (s == NULL)
	as_bad (_(".set pop with no .set push"));
      else
	{
	  /* If we're changing the reorder mode we need to handle
             delay slots correctly.  */
	  if (s->options.noreorder && ! mips_opts.noreorder)
	    mips_emit_delays (TRUE);
	  else if (! s->options.noreorder && mips_opts.noreorder)
	    {
	      if (prev_nop_frag != NULL)
		{
		  prev_nop_frag->fr_fix -= (prev_nop_frag_holds
					    * (mips_opts.mips16 ? 2 : 4));
		  prev_nop_frag = NULL;
		}
	    }

	  mips_opts = s->options;
	  mips_opts_stack = s->next;
	  free (s);
	}
    }
  else
    {
      as_warn (_("Tried to set unrecognized symbol: %s\n"), name);
    }
  *input_line_pointer = ch;
  demand_empty_rest_of_line ();
}

/* Handle the .abicalls pseudo-op.  I believe this is equivalent to
   .option pic2.  It means to generate SVR4 PIC calls.  */

static void
s_abicalls (int ignore ATTRIBUTE_UNUSED)
{
  mips_pic = SVR4_PIC;
  mips_abicalls = TRUE;
  if (USE_GLOBAL_POINTER_OPT)
    {
      if (g_switch_seen && g_switch_value != 0)
	as_warn (_("-G may not be used with SVR4 PIC code"));
      g_switch_value = 0;
    }
  bfd_set_gp_size (stdoutput, 0);
  demand_empty_rest_of_line ();
}

/* Handle the .cpload pseudo-op.  This is used when generating SVR4
   PIC code.  It sets the $gp register for the function based on the
   function address, which is in the register named in the argument.
   This uses a relocation against _gp_disp, which is handled specially
   by the linker.  The result is:
	lui	$gp,%hi(_gp_disp)
	addiu	$gp,$gp,%lo(_gp_disp)
	addu	$gp,$gp,.cpload argument
   The .cpload argument is normally $25 == $t9.  */

static void
s_cpload (int ignore ATTRIBUTE_UNUSED)
{
  expressionS ex;

  /* If we are not generating SVR4 PIC code, or if this is NewABI code,
     .cpload is ignored.  */
  if (mips_pic != SVR4_PIC || HAVE_NEWABI)
    {
      s_ignore (0);
      return;
    }

  /* .cpload should be in a .set noreorder section.  */
  if (mips_opts.noreorder == 0)
    as_warn (_(".cpload not in noreorder section"));

  ex.X_op = O_symbol;
  ex.X_add_symbol = symbol_find_or_make ("_gp_disp");
  ex.X_op_symbol = NULL;
  ex.X_add_number = 0;

  /* In ELF, this symbol is implicitly an STT_OBJECT symbol.  */
  symbol_get_bfdsym (ex.X_add_symbol)->flags |= BSF_OBJECT;

  macro_start ();
  macro_build_lui (&ex, mips_gp_register);
  macro_build (&ex, "addiu", "t,r,j", mips_gp_register,
	       mips_gp_register, BFD_RELOC_LO16);
  macro_build (NULL, "addu", "d,v,t", mips_gp_register,
	       mips_gp_register, tc_get_register (0));
  macro_end ();

  demand_empty_rest_of_line ();
}

/* Handle the .cpsetup pseudo-op defined for NewABI PIC code.  The syntax is:
     .cpsetup $reg1, offset|$reg2, label

   If offset is given, this results in:
     sd		$gp, offset($sp)
     lui	$gp, %hi(%neg(%gp_rel(label)))
     addiu	$gp, $gp, %lo(%neg(%gp_rel(label)))
     daddu	$gp, $gp, $reg1

   If $reg2 is given, this results in:
     daddu	$reg2, $gp, $0
     lui	$gp, %hi(%neg(%gp_rel(label)))
     addiu	$gp, $gp, %lo(%neg(%gp_rel(label)))
     daddu	$gp, $gp, $reg1
   $reg1 is normally $25 == $t9.  */
static void
s_cpsetup (int ignore ATTRIBUTE_UNUSED)
{
  expressionS ex_off;
  expressionS ex_sym;
  int reg1;
  char *f;

  /* If we are not generating SVR4 PIC code, .cpsetup is ignored.
     We also need NewABI support.  */
  if (mips_pic != SVR4_PIC || ! HAVE_NEWABI)
    {
      s_ignore (0);
      return;
    }

  reg1 = tc_get_register (0);
  SKIP_WHITESPACE ();
  if (*input_line_pointer != ',')
    {
      as_bad (_("missing argument separator ',' for .cpsetup"));
      return;
    }
  else
    ++input_line_pointer;
  SKIP_WHITESPACE ();
  if (*input_line_pointer == '$')
    {
      mips_cpreturn_register = tc_get_register (0);
      mips_cpreturn_offset = -1;
    }
  else
    {
      mips_cpreturn_offset = get_absolute_expression ();
      mips_cpreturn_register = -1;
    }
  SKIP_WHITESPACE ();
  if (*input_line_pointer != ',')
    {
      as_bad (_("missing argument separator ',' for .cpsetup"));
      return;
    }
  else
    ++input_line_pointer;
  SKIP_WHITESPACE ();
  expression (&ex_sym);

  macro_start ();
  if (mips_cpreturn_register == -1)
    {
      ex_off.X_op = O_constant;
      ex_off.X_add_symbol = NULL;
      ex_off.X_op_symbol = NULL;
      ex_off.X_add_number = mips_cpreturn_offset;

      macro_build (&ex_off, "sd", "t,o(b)", mips_gp_register,
		   BFD_RELOC_LO16, SP);
    }
  else
    macro_build (NULL, "daddu", "d,v,t", mips_cpreturn_register,
		 mips_gp_register, 0);

  /* Ensure there's room for the next two instructions, so that `f'
     doesn't end up with an address in the wrong frag.  */
  frag_grow (8);
  f = frag_more (0);
  macro_build (&ex_sym, "lui", "t,u", mips_gp_register, BFD_RELOC_GPREL16);
  fix_new (frag_now, f - frag_now->fr_literal,
	   8, NULL, 0, 0, BFD_RELOC_MIPS_SUB);
  fix_new (frag_now, f - frag_now->fr_literal,
	   4, NULL, 0, 0, BFD_RELOC_HI16_S);

  f = frag_more (0);
  macro_build (&ex_sym, "addiu", "t,r,j", mips_gp_register,
	       mips_gp_register, BFD_RELOC_GPREL16);
  fix_new (frag_now, f - frag_now->fr_literal,
	   8, NULL, 0, 0, BFD_RELOC_MIPS_SUB);
  fix_new (frag_now, f - frag_now->fr_literal,
	   4, NULL, 0, 0, BFD_RELOC_LO16);

  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", mips_gp_register,
	       mips_gp_register, reg1);
  macro_end ();

  demand_empty_rest_of_line ();
}

static void
s_cplocal (int ignore ATTRIBUTE_UNUSED)
{
  /* If we are not generating SVR4 PIC code, or if this is not NewABI code,
   .cplocal is ignored.  */
  if (mips_pic != SVR4_PIC || ! HAVE_NEWABI)
    {
      s_ignore (0);
      return;
    }

  mips_gp_register = tc_get_register (0);
  demand_empty_rest_of_line ();
}

/* Handle the .cprestore pseudo-op.  This stores $gp into a given
   offset from $sp.  The offset is remembered, and after making a PIC
   call $gp is restored from that location.  */

static void
s_cprestore (int ignore ATTRIBUTE_UNUSED)
{
  expressionS ex;

  /* If we are not generating SVR4 PIC code, or if this is NewABI code,
     .cprestore is ignored.  */
  if (mips_pic != SVR4_PIC || HAVE_NEWABI)
    {
      s_ignore (0);
      return;
    }

  mips_cprestore_offset = get_absolute_expression ();
  mips_cprestore_valid = 1;

  ex.X_op = O_constant;
  ex.X_add_symbol = NULL;
  ex.X_op_symbol = NULL;
  ex.X_add_number = mips_cprestore_offset;

  macro_start ();
  macro_build_ldst_constoffset (&ex, ADDRESS_STORE_INSN, mips_gp_register,
				SP, HAVE_64BIT_ADDRESSES);
  macro_end ();

  demand_empty_rest_of_line ();
}

/* Handle the .cpreturn pseudo-op defined for NewABI PIC code. If an offset
   was given in the preceding .cpsetup, it results in:
     ld		$gp, offset($sp)

   If a register $reg2 was given there, it results in:
     daddu	$gp, $reg2, $0
 */
static void
s_cpreturn (int ignore ATTRIBUTE_UNUSED)
{
  expressionS ex;

  /* If we are not generating SVR4 PIC code, .cpreturn is ignored.
     We also need NewABI support.  */
  if (mips_pic != SVR4_PIC || ! HAVE_NEWABI)
    {
      s_ignore (0);
      return;
    }

  macro_start ();
  if (mips_cpreturn_register == -1)
    {
      ex.X_op = O_constant;
      ex.X_add_symbol = NULL;
      ex.X_op_symbol = NULL;
      ex.X_add_number = mips_cpreturn_offset;

      macro_build (&ex, "ld", "t,o(b)", mips_gp_register, BFD_RELOC_LO16, SP);
    }
  else
    macro_build (NULL, "daddu", "d,v,t", mips_gp_register,
		 mips_cpreturn_register, 0);
  macro_end ();

  demand_empty_rest_of_line ();
}

/* Handle the .gpvalue pseudo-op.  This is used when generating NewABI PIC
   code.  It sets the offset to use in gp_rel relocations.  */

static void
s_gpvalue (int ignore ATTRIBUTE_UNUSED)
{
  /* If we are not generating SVR4 PIC code, .gpvalue is ignored.
     We also need NewABI support.  */
  if (mips_pic != SVR4_PIC || ! HAVE_NEWABI)
    {
      s_ignore (0);
      return;
    }

  mips_gprel_offset = get_absolute_expression ();

  demand_empty_rest_of_line ();
}

/* Handle the .gpword pseudo-op.  This is used when generating PIC
   code.  It generates a 32 bit GP relative reloc.  */

static void
s_gpword (int ignore ATTRIBUTE_UNUSED)
{
  symbolS *label;
  expressionS ex;
  char *p;

  /* When not generating PIC code, this is treated as .word.  */
  if (mips_pic != SVR4_PIC)
    {
      s_cons (2);
      return;
    }

  label = insn_labels != NULL ? insn_labels->label : NULL;
  mips_emit_delays (TRUE);
  if (auto_align)
    mips_align (2, 0, label);
  mips_clear_insn_labels ();

  expression (&ex);

  if (ex.X_op != O_symbol || ex.X_add_number != 0)
    {
      as_bad (_("Unsupported use of .gpword"));
      ignore_rest_of_line ();
    }

  p = frag_more (4);
  md_number_to_chars (p, 0, 4);
  fix_new_exp (frag_now, p - frag_now->fr_literal, 4, &ex, FALSE,
	       BFD_RELOC_GPREL32);

  demand_empty_rest_of_line ();
}

static void
s_gpdword (int ignore ATTRIBUTE_UNUSED)
{
  symbolS *label;
  expressionS ex;
  char *p;

  /* When not generating PIC code, this is treated as .dword.  */
  if (mips_pic != SVR4_PIC)
    {
      s_cons (3);
      return;
    }

  label = insn_labels != NULL ? insn_labels->label : NULL;
  mips_emit_delays (TRUE);
  if (auto_align)
    mips_align (3, 0, label);
  mips_clear_insn_labels ();

  expression (&ex);

  if (ex.X_op != O_symbol || ex.X_add_number != 0)
    {
      as_bad (_("Unsupported use of .gpdword"));
      ignore_rest_of_line ();
    }

  p = frag_more (8);
  md_number_to_chars (p, 0, 8);
  fix_new_exp (frag_now, p - frag_now->fr_literal, 4, &ex, FALSE,
	       BFD_RELOC_GPREL32);

  /* GPREL32 composed with 64 gives a 64-bit GP offset.  */
  ex.X_op = O_absent;
  ex.X_add_symbol = 0;
  ex.X_add_number = 0;
  fix_new_exp (frag_now, p - frag_now->fr_literal, 8, &ex, FALSE,
	       BFD_RELOC_64);

  demand_empty_rest_of_line ();
}

/* Handle the .cpadd pseudo-op.  This is used when dealing with switch
   tables in SVR4 PIC code.  */

static void
s_cpadd (int ignore ATTRIBUTE_UNUSED)
{
  int reg;

  /* This is ignored when not generating SVR4 PIC code.  */
  if (mips_pic != SVR4_PIC)
    {
      s_ignore (0);
      return;
    }

  /* Add $gp to the register named as an argument.  */
  macro_start ();
  reg = tc_get_register (0);
  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", reg, reg, mips_gp_register);
  macro_end ();

  demand_empty_rest_of_line ();
}

/* Handle the .insn pseudo-op.  This marks instruction labels in
   mips16 mode.  This permits the linker to handle them specially,
   such as generating jalx instructions when needed.  We also make
   them odd for the duration of the assembly, in order to generate the
   right sort of code.  We will make them even in the adjust_symtab
   routine, while leaving them marked.  This is convenient for the
   debugger and the disassembler.  The linker knows to make them odd
   again.  */

static void
s_insn (int ignore ATTRIBUTE_UNUSED)
{
  mips16_mark_labels ();

  demand_empty_rest_of_line ();
}

/* Handle a .stabn directive.  We need these in order to mark a label
   as being a mips16 text label correctly.  Sometimes the compiler
   will emit a label, followed by a .stabn, and then switch sections.
   If the label and .stabn are in mips16 mode, then the label is
   really a mips16 text label.  */

static void
s_mips_stab (int type)
{
  if (type == 'n')
    mips16_mark_labels ();

  s_stab (type);
}

/* Handle the .weakext pseudo-op as defined in Kane and Heinrich.
 */

static void
s_mips_weakext (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  int c;
  symbolS *symbolP;
  expressionS exp;

  name = input_line_pointer;
  c = get_symbol_end ();
  symbolP = symbol_find_or_make (name);
  S_SET_WEAK (symbolP);
  *input_line_pointer = c;

  SKIP_WHITESPACE ();

  if (! is_end_of_line[(unsigned char) *input_line_pointer])
    {
      if (S_IS_DEFINED (symbolP))
	{
	  as_bad ("ignoring attempt to redefine symbol %s",
		  S_GET_NAME (symbolP));
	  ignore_rest_of_line ();
	  return;
	}

      if (*input_line_pointer == ',')
	{
	  ++input_line_pointer;
	  SKIP_WHITESPACE ();
	}

      expression (&exp);
      if (exp.X_op != O_symbol)
	{
	  as_bad ("bad .weakext directive");
	  ignore_rest_of_line ();
	  return;
	}
      symbol_set_value_expression (symbolP, &exp);
    }

  demand_empty_rest_of_line ();
}

/* Parse a register string into a number.  Called from the ECOFF code
   to parse .frame.  The argument is non-zero if this is the frame
   register, so that we can record it in mips_frame_reg.  */

int
tc_get_register (int frame)
{
  int reg;

  SKIP_WHITESPACE ();
  if (*input_line_pointer++ != '$')
    {
      as_warn (_("expected `$'"));
      reg = ZERO;
    }
  else if (ISDIGIT (*input_line_pointer))
    {
      reg = get_absolute_expression ();
      if (reg < 0 || reg >= 32)
	{
	  as_warn (_("Bad register number"));
	  reg = ZERO;
	}
    }
  else
    {
      if (strncmp (input_line_pointer, "ra", 2) == 0)
	{
	  reg = RA;
	  input_line_pointer += 2;
	}
      else if (strncmp (input_line_pointer, "fp", 2) == 0)
	{
	  reg = FP;
	  input_line_pointer += 2;
	}
      else if (strncmp (input_line_pointer, "sp", 2) == 0)
	{
	  reg = SP;
	  input_line_pointer += 2;
	}
      else if (strncmp (input_line_pointer, "gp", 2) == 0)
	{
	  reg = GP;
	  input_line_pointer += 2;
	}
      else if (strncmp (input_line_pointer, "at", 2) == 0)
	{
	  reg = AT;
	  input_line_pointer += 2;
	}
      else if (strncmp (input_line_pointer, "kt0", 3) == 0)
	{
	  reg = KT0;
	  input_line_pointer += 3;
	}
      else if (strncmp (input_line_pointer, "kt1", 3) == 0)
	{
	  reg = KT1;
	  input_line_pointer += 3;
	}
      else if (strncmp (input_line_pointer, "zero", 4) == 0)
	{
	  reg = ZERO;
	  input_line_pointer += 4;
	}
      else
	{
	  as_warn (_("Unrecognized register name"));
	  reg = ZERO;
	  while (ISALNUM(*input_line_pointer))
	   input_line_pointer++;
	}
    }
  if (frame)
    {
      mips_frame_reg = reg != 0 ? reg : SP;
      mips_frame_reg_valid = 1;
      mips_cprestore_valid = 0;
    }
  return reg;
}

valueT
md_section_align (asection *seg, valueT addr)
{
  int align = bfd_get_section_alignment (stdoutput, seg);

#ifdef OBJ_ELF
  /* We don't need to align ELF sections to the full alignment.
     However, Irix 5 may prefer that we align them at least to a 16
     byte boundary.  We don't bother to align the sections if we are
     targeted for an embedded system.  */
  if (strcmp (TARGET_OS, "elf") == 0)
    return addr;
  if (align > 4)
    align = 4;
#endif

  return ((addr + (1 << align) - 1) & (-1 << align));
}

/* Utility routine, called from above as well.  If called while the
   input file is still being read, it's only an approximation.  (For
   example, a symbol may later become defined which appeared to be
   undefined earlier.)  */

static int
nopic_need_relax (symbolS *sym, int before_relaxing)
{
  if (sym == 0)
    return 0;

  if (USE_GLOBAL_POINTER_OPT && g_switch_value > 0)
    {
      const char *symname;
      int change;

      /* Find out whether this symbol can be referenced off the $gp
	 register.  It can be if it is smaller than the -G size or if
	 it is in the .sdata or .sbss section.  Certain symbols can
	 not be referenced off the $gp, although it appears as though
	 they can.  */
      symname = S_GET_NAME (sym);
      if (symname != (const char *) NULL
	  && (strcmp (symname, "eprol") == 0
	      || strcmp (symname, "etext") == 0
	      || strcmp (symname, "_gp") == 0
	      || strcmp (symname, "edata") == 0
	      || strcmp (symname, "_fbss") == 0
	      || strcmp (symname, "_fdata") == 0
	      || strcmp (symname, "_ftext") == 0
	      || strcmp (symname, "end") == 0
	      || strcmp (symname, "_gp_disp") == 0))
	change = 1;
      else if ((! S_IS_DEFINED (sym) || S_IS_COMMON (sym))
	       && (0
#ifndef NO_ECOFF_DEBUGGING
		   || (symbol_get_obj (sym)->ecoff_extern_size != 0
		       && (symbol_get_obj (sym)->ecoff_extern_size
			   <= g_switch_value))
#endif
		   /* We must defer this decision until after the whole
		      file has been read, since there might be a .extern
		      after the first use of this symbol.  */
		   || (before_relaxing
#ifndef NO_ECOFF_DEBUGGING
		       && symbol_get_obj (sym)->ecoff_extern_size == 0
#endif
		       && S_GET_VALUE (sym) == 0)
		   || (S_GET_VALUE (sym) != 0
		       && S_GET_VALUE (sym) <= g_switch_value)))
	change = 0;
      else
	{
	  const char *segname;

	  segname = segment_name (S_GET_SEGMENT (sym));
	  assert (strcmp (segname, ".lit8") != 0
		  && strcmp (segname, ".lit4") != 0);
	  change = (strcmp (segname, ".sdata") != 0
		    && strcmp (segname, ".sbss") != 0
		    && strncmp (segname, ".sdata.", 7) != 0
		    && strncmp (segname, ".gnu.linkonce.s.", 16) != 0);
	}
      return change;
    }
  else
    /* We are not optimizing for the $gp register.  */
    return 1;
}


/* Return true if the given symbol should be considered local for SVR4 PIC.  */

static bfd_boolean
pic_need_relax (symbolS *sym, asection *segtype)
{
  asection *symsec;
  bfd_boolean linkonce;

  /* Handle the case of a symbol equated to another symbol.  */
  while (symbol_equated_reloc_p (sym))
    {
      symbolS *n;

      /* It's possible to get a loop here in a badly written
	 program.  */
      n = symbol_get_value_expression (sym)->X_add_symbol;
      if (n == sym)
	break;
      sym = n;
    }

  symsec = S_GET_SEGMENT (sym);

  /* duplicate the test for LINK_ONCE sections as in adjust_reloc_syms */
  linkonce = FALSE;
  if (symsec != segtype && ! S_IS_LOCAL (sym))
    {
      if ((bfd_get_section_flags (stdoutput, symsec) & SEC_LINK_ONCE)
	  != 0)
	linkonce = TRUE;

      /* The GNU toolchain uses an extension for ELF: a section
	 beginning with the magic string .gnu.linkonce is a linkonce
	 section.  */
      if (strncmp (segment_name (symsec), ".gnu.linkonce",
		   sizeof ".gnu.linkonce" - 1) == 0)
	linkonce = TRUE;
    }

  /* This must duplicate the test in adjust_reloc_syms.  */
  return (symsec != &bfd_und_section
	  && symsec != &bfd_abs_section
	  && ! bfd_is_com_section (symsec)
	  && !linkonce
#ifdef OBJ_ELF
	  /* A global or weak symbol is treated as external.  */
	  && (OUTPUT_FLAVOR != bfd_target_elf_flavour
	      || (! S_IS_WEAK (sym)
		  && (! S_IS_EXTERNAL (sym)
		      || mips_pic == EMBEDDED_PIC)))
#endif
	  );
}


/* Given a mips16 variant frag FRAGP, return non-zero if it needs an
   extended opcode.  SEC is the section the frag is in.  */

static int
mips16_extended_frag (fragS *fragp, asection *sec, long stretch)
{
  int type;
  register const struct mips16_immed_operand *op;
  offsetT val;
  int mintiny, maxtiny;
  segT symsec;
  fragS *sym_frag;

  if (RELAX_MIPS16_USER_SMALL (fragp->fr_subtype))
    return 0;
  if (RELAX_MIPS16_USER_EXT (fragp->fr_subtype))
    return 1;

  type = RELAX_MIPS16_TYPE (fragp->fr_subtype);
  op = mips16_immed_operands;
  while (op->type != type)
    {
      ++op;
      assert (op < mips16_immed_operands + MIPS16_NUM_IMMED);
    }

  if (op->unsp)
    {
      if (type == '<' || type == '>' || type == '[' || type == ']')
	{
	  mintiny = 1;
	  maxtiny = 1 << op->nbits;
	}
      else
	{
	  mintiny = 0;
	  maxtiny = (1 << op->nbits) - 1;
	}
    }
  else
    {
      mintiny = - (1 << (op->nbits - 1));
      maxtiny = (1 << (op->nbits - 1)) - 1;
    }

  sym_frag = symbol_get_frag (fragp->fr_symbol);
  val = S_GET_VALUE (fragp->fr_symbol);
  symsec = S_GET_SEGMENT (fragp->fr_symbol);

  if (op->pcrel)
    {
      addressT addr;

      /* We won't have the section when we are called from
         mips_relax_frag.  However, we will always have been called
         from md_estimate_size_before_relax first.  If this is a
         branch to a different section, we mark it as such.  If SEC is
         NULL, and the frag is not marked, then it must be a branch to
         the same section.  */
      if (sec == NULL)
	{
	  if (RELAX_MIPS16_LONG_BRANCH (fragp->fr_subtype))
	    return 1;
	}
      else
	{
	  /* Must have been called from md_estimate_size_before_relax.  */
	  if (symsec != sec)
	    {
	      fragp->fr_subtype =
		RELAX_MIPS16_MARK_LONG_BRANCH (fragp->fr_subtype);

	      /* FIXME: We should support this, and let the linker
                 catch branches and loads that are out of range.  */
	      as_bad_where (fragp->fr_file, fragp->fr_line,
			    _("unsupported PC relative reference to different section"));

	      return 1;
	    }
	  if (fragp != sym_frag && sym_frag->fr_address == 0)
	    /* Assume non-extended on the first relaxation pass.
	       The address we have calculated will be bogus if this is
	       a forward branch to another frag, as the forward frag
	       will have fr_address == 0.  */
	    return 0;
	}

      /* In this case, we know for sure that the symbol fragment is in
	 the same section.  If the relax_marker of the symbol fragment
	 differs from the relax_marker of this fragment, we have not
	 yet adjusted the symbol fragment fr_address.  We want to add
	 in STRETCH in order to get a better estimate of the address.
	 This particularly matters because of the shift bits.  */
      if (stretch != 0
	  && sym_frag->relax_marker != fragp->relax_marker)
	{
	  fragS *f;

	  /* Adjust stretch for any alignment frag.  Note that if have
             been expanding the earlier code, the symbol may be
             defined in what appears to be an earlier frag.  FIXME:
             This doesn't handle the fr_subtype field, which specifies
             a maximum number of bytes to skip when doing an
             alignment.  */
	  for (f = fragp; f != NULL && f != sym_frag; f = f->fr_next)
	    {
	      if (f->fr_type == rs_align || f->fr_type == rs_align_code)
		{
		  if (stretch < 0)
		    stretch = - ((- stretch)
				 & ~ ((1 << (int) f->fr_offset) - 1));
		  else
		    stretch &= ~ ((1 << (int) f->fr_offset) - 1);
		  if (stretch == 0)
		    break;
		}
	    }
	  if (f != NULL)
	    val += stretch;
	}

      addr = fragp->fr_address + fragp->fr_fix;

      /* The base address rules are complicated.  The base address of
         a branch is the following instruction.  The base address of a
         PC relative load or add is the instruction itself, but if it
         is in a delay slot (in which case it can not be extended) use
         the address of the instruction whose delay slot it is in.  */
      if (type == 'p' || type == 'q')
	{
	  addr += 2;

	  /* If we are currently assuming that this frag should be
	     extended, then, the current address is two bytes
	     higher.  */
	  if (RELAX_MIPS16_EXTENDED (fragp->fr_subtype))
	    addr += 2;

	  /* Ignore the low bit in the target, since it will be set
             for a text label.  */
	  if ((val & 1) != 0)
	    --val;
	}
      else if (RELAX_MIPS16_JAL_DSLOT (fragp->fr_subtype))
	addr -= 4;
      else if (RELAX_MIPS16_DSLOT (fragp->fr_subtype))
	addr -= 2;

      val -= addr & ~ ((1 << op->shift) - 1);

      /* Branch offsets have an implicit 0 in the lowest bit.  */
      if (type == 'p' || type == 'q')
	val /= 2;

      /* If any of the shifted bits are set, we must use an extended
         opcode.  If the address depends on the size of this
         instruction, this can lead to a loop, so we arrange to always
         use an extended opcode.  We only check this when we are in
         the main relaxation loop, when SEC is NULL.  */
      if ((val & ((1 << op->shift) - 1)) != 0 && sec == NULL)
	{
	  fragp->fr_subtype =
	    RELAX_MIPS16_MARK_LONG_BRANCH (fragp->fr_subtype);
	  return 1;
	}

      /* If we are about to mark a frag as extended because the value
         is precisely maxtiny + 1, then there is a chance of an
         infinite loop as in the following code:
	     la	$4,foo
	     .skip	1020
	     .align	2
	   foo:
	 In this case when the la is extended, foo is 0x3fc bytes
	 away, so the la can be shrunk, but then foo is 0x400 away, so
	 the la must be extended.  To avoid this loop, we mark the
	 frag as extended if it was small, and is about to become
	 extended with a value of maxtiny + 1.  */
      if (val == ((maxtiny + 1) << op->shift)
	  && ! RELAX_MIPS16_EXTENDED (fragp->fr_subtype)
	  && sec == NULL)
	{
	  fragp->fr_subtype =
	    RELAX_MIPS16_MARK_LONG_BRANCH (fragp->fr_subtype);
	  return 1;
	}
    }
  else if (symsec != absolute_section && sec != NULL)
    as_bad_where (fragp->fr_file, fragp->fr_line, _("unsupported relocation"));

  if ((val & ((1 << op->shift) - 1)) != 0
      || val < (mintiny << op->shift)
      || val > (maxtiny << op->shift))
    return 1;
  else
    return 0;
}

/* Compute the length of a branch sequence, and adjust the
   RELAX_BRANCH_TOOFAR bit accordingly.  If FRAGP is NULL, the
   worst-case length is computed, with UPDATE being used to indicate
   whether an unconditional (-1), branch-likely (+1) or regular (0)
   branch is to be computed.  */
static int
relaxed_branch_length (fragS *fragp, asection *sec, int update)
{
  bfd_boolean toofar;
  int length;

  if (fragp
      && S_IS_DEFINED (fragp->fr_symbol)
      && sec == S_GET_SEGMENT (fragp->fr_symbol))
    {
      addressT addr;
      offsetT val;

      val = S_GET_VALUE (fragp->fr_symbol) + fragp->fr_offset;

      addr = fragp->fr_address + fragp->fr_fix + 4;

      val -= addr;

      toofar = val < - (0x8000 << 2) || val >= (0x8000 << 2);
    }
  else if (fragp)
    /* If the symbol is not defined or it's in a different segment,
       assume the user knows what's going on and emit a short
       branch.  */
    toofar = FALSE;
  else
    toofar = TRUE;

  if (fragp && update && toofar != RELAX_BRANCH_TOOFAR (fragp->fr_subtype))
    fragp->fr_subtype
      = RELAX_BRANCH_ENCODE (RELAX_BRANCH_UNCOND (fragp->fr_subtype),
			     RELAX_BRANCH_LIKELY (fragp->fr_subtype),
			     RELAX_BRANCH_LINK (fragp->fr_subtype),
			     toofar);

  length = 4;
  if (toofar)
    {
      if (fragp ? RELAX_BRANCH_LIKELY (fragp->fr_subtype) : (update > 0))
	length += 8;

      if (mips_pic != NO_PIC)
	{
	  /* Additional space for PIC loading of target address.  */
	  length += 8;
	  if (mips_opts.isa == ISA_MIPS1)
	    /* Additional space for $at-stabilizing nop.  */
	    length += 4;
	}

      /* If branch is conditional.  */
      if (fragp ? !RELAX_BRANCH_UNCOND (fragp->fr_subtype) : (update >= 0))
	length += 8;
    }

  return length;
}

/* Estimate the size of a frag before relaxing.  Unless this is the
   mips16, we are not really relaxing here, and the final size is
   encoded in the subtype information.  For the mips16, we have to
   decide whether we are using an extended opcode or not.  */

int
md_estimate_size_before_relax (fragS *fragp, asection *segtype)
{
  int change;

  if (RELAX_BRANCH_P (fragp->fr_subtype))
    {

      fragp->fr_var = relaxed_branch_length (fragp, segtype, FALSE);

      return fragp->fr_var;
    }

  if (RELAX_MIPS16_P (fragp->fr_subtype))
    /* We don't want to modify the EXTENDED bit here; it might get us
       into infinite loops.  We change it only in mips_relax_frag().  */
    return (RELAX_MIPS16_EXTENDED (fragp->fr_subtype) ? 4 : 2);

  if (mips_pic == NO_PIC)
    change = nopic_need_relax (fragp->fr_symbol, 0);
  else if (mips_pic == SVR4_PIC)
    change = pic_need_relax (fragp->fr_symbol, segtype);
  else
    abort ();

  if (change)
    {
      fragp->fr_subtype |= RELAX_USE_SECOND;
      return -RELAX_FIRST (fragp->fr_subtype);
    }
  else
    return -RELAX_SECOND (fragp->fr_subtype);
}

/* This is called to see whether a reloc against a defined symbol
   should be converted into a reloc against a section.  Don't adjust
   MIPS16 jump relocations, so we don't have to worry about the format
   of the offset in the .o file.  Don't adjust relocations against
   mips16 symbols, so that the linker can find them if it needs to set
   up a stub.  */

int
mips_fix_adjustable (fixS *fixp)
{
  if (fixp->fx_r_type == BFD_RELOC_MIPS16_JMP)
    return 0;

  if (fixp->fx_r_type == BFD_RELOC_VTABLE_INHERIT
      || fixp->fx_r_type == BFD_RELOC_VTABLE_ENTRY)
    return 0;

  if (fixp->fx_addsy == NULL)
    return 1;

#ifdef OBJ_ELF
  if (OUTPUT_FLAVOR == bfd_target_elf_flavour
      && S_GET_OTHER (fixp->fx_addsy) == STO_MIPS16
      && fixp->fx_subsy == NULL)
    return 0;
#endif

  return 1;
}

/* Translate internal representation of relocation info to BFD target
   format.  */

arelent **
tc_gen_reloc (asection *section ATTRIBUTE_UNUSED, fixS *fixp)
{
  static arelent *retval[4];
  arelent *reloc;
  bfd_reloc_code_real_type code;

  memset (retval, 0, sizeof(retval));
  reloc = retval[0] = (arelent *) xcalloc (1, sizeof (arelent));
  reloc->sym_ptr_ptr = (asymbol **) xmalloc (sizeof (asymbol *));
  *reloc->sym_ptr_ptr = symbol_get_bfdsym (fixp->fx_addsy);
  reloc->address = fixp->fx_frag->fr_address + fixp->fx_where;

  if (mips_pic == EMBEDDED_PIC
      && SWITCH_TABLE (fixp))
    {
      /* For a switch table entry we use a special reloc.  The addend
	 is actually the difference between the reloc address and the
	 subtrahend.  */
      reloc->addend = reloc->address - S_GET_VALUE (fixp->fx_subsy);
      if (OUTPUT_FLAVOR != bfd_target_ecoff_flavour)
	as_fatal (_("Double check fx_r_type in tc-mips.c:tc_gen_reloc"));
      fixp->fx_r_type = BFD_RELOC_GPREL32;
    }
  else if (fixp->fx_pcrel)
    {
      bfd_vma pcrel_address;

      /* Set PCREL_ADDRESS to this relocation's "PC".  The PC for high
	 high-part relocs is the address of the low-part reloc.  */
      if (fixp->fx_r_type == BFD_RELOC_PCREL_HI16_S)
	{
	  assert (fixp->fx_next != NULL
		  && fixp->fx_next->fx_r_type == BFD_RELOC_PCREL_LO16);
	  pcrel_address = (fixp->fx_next->fx_where
			   + fixp->fx_next->fx_frag->fr_address);
	}
      else
	pcrel_address = reloc->address;

      if (OUTPUT_FLAVOR == bfd_target_elf_flavour)
	{
	  /* At this point, fx_addnumber is "symbol offset - pcrel_address".
	     Relocations want only the symbol offset.  */
	  reloc->addend = fixp->fx_addnumber + pcrel_address;
	}
      else if (fixp->fx_r_type == BFD_RELOC_PCREL_LO16
	       || fixp->fx_r_type == BFD_RELOC_PCREL_HI16_S)
	{
	  /* We use a special addend for an internal RELLO or RELHI reloc.  */
	  if (symbol_section_p (fixp->fx_addsy))
	    reloc->addend = pcrel_address - S_GET_VALUE (fixp->fx_subsy);
	  else
	    reloc->addend = fixp->fx_addnumber + pcrel_address;
	}
      else
	{
	  if (OUTPUT_FLAVOR != bfd_target_aout_flavour)
	    /* A gruesome hack which is a result of the gruesome gas reloc
	       handling.  */
	    reloc->addend = pcrel_address;
	  else
	    reloc->addend = -pcrel_address;
	}
    }
  else
    reloc->addend = fixp->fx_addnumber;

  /* Since the old MIPS ELF ABI uses Rel instead of Rela, encode the vtable
     entry to be used in the relocation's section offset.  */
  if (! HAVE_NEWABI && fixp->fx_r_type == BFD_RELOC_VTABLE_ENTRY)
    {
      reloc->address = reloc->addend;
      reloc->addend = 0;
    }

  /* Since DIFF_EXPR_OK is defined in tc-mips.h, it is possible that
     fixup_segment converted a non-PC relative reloc into a PC
     relative reloc.  In such a case, we need to convert the reloc
     code.  */
  code = fixp->fx_r_type;
  if (fixp->fx_pcrel)
    {
      switch (code)
	{
	case BFD_RELOC_8:
	  code = BFD_RELOC_8_PCREL;
	  break;
	case BFD_RELOC_16:
	  code = BFD_RELOC_16_PCREL;
	  break;
	case BFD_RELOC_32:
	  code = BFD_RELOC_32_PCREL;
	  break;
	case BFD_RELOC_64:
	  code = BFD_RELOC_64_PCREL;
	  break;
	case BFD_RELOC_8_PCREL:
	case BFD_RELOC_16_PCREL:
	case BFD_RELOC_32_PCREL:
	case BFD_RELOC_64_PCREL:
	case BFD_RELOC_16_PCREL_S2:
	case BFD_RELOC_PCREL_HI16_S:
	case BFD_RELOC_PCREL_LO16:
	  break;
	default:
	  as_bad_where (fixp->fx_file, fixp->fx_line,
			_("Cannot make %s relocation PC relative"),
			bfd_get_reloc_code_name (code));
	}
    }

  /* To support a PC relative reloc when generating embedded PIC code
     for ECOFF, we use a Cygnus extension.  We check for that here to
     make sure that we don't let such a reloc escape normally.  */
  if ((OUTPUT_FLAVOR == bfd_target_ecoff_flavour
       || OUTPUT_FLAVOR == bfd_target_elf_flavour)
      && code == BFD_RELOC_16_PCREL_S2
      && mips_pic != EMBEDDED_PIC)
    reloc->howto = NULL;
  else
    reloc->howto = bfd_reloc_type_lookup (stdoutput, code);

  if (reloc->howto == NULL)
    {
      as_bad_where (fixp->fx_file, fixp->fx_line,
		    _("Can not represent %s relocation in this object file format"),
		    bfd_get_reloc_code_name (code));
      retval[0] = NULL;
    }

  return retval;
}

/* Relax a machine dependent frag.  This returns the amount by which
   the current size of the frag should change.  */

int
mips_relax_frag (asection *sec, fragS *fragp, long stretch)
{
  if (RELAX_BRANCH_P (fragp->fr_subtype))
    {
      offsetT old_var = fragp->fr_var;

      fragp->fr_var = relaxed_branch_length (fragp, sec, TRUE);

      return fragp->fr_var - old_var;
    }

  if (! RELAX_MIPS16_P (fragp->fr_subtype))
    return 0;

  if (mips16_extended_frag (fragp, NULL, stretch))
    {
      if (RELAX_MIPS16_EXTENDED (fragp->fr_subtype))
	return 0;
      fragp->fr_subtype = RELAX_MIPS16_MARK_EXTENDED (fragp->fr_subtype);
      return 2;
    }
  else
    {
      if (! RELAX_MIPS16_EXTENDED (fragp->fr_subtype))
	return 0;
      fragp->fr_subtype = RELAX_MIPS16_CLEAR_EXTENDED (fragp->fr_subtype);
      return -2;
    }

  return 0;
}

/* Convert a machine dependent frag.  */

void
md_convert_frag (bfd *abfd ATTRIBUTE_UNUSED, segT asec, fragS *fragp)
{
  if (RELAX_BRANCH_P (fragp->fr_subtype))
    {
      bfd_byte *buf;
      unsigned long insn;
      expressionS exp;
      fixS *fixp;

      buf = (bfd_byte *)fragp->fr_literal + fragp->fr_fix;

      if (target_big_endian)
	insn = bfd_getb32 (buf);
      else
	insn = bfd_getl32 (buf);

      if (!RELAX_BRANCH_TOOFAR (fragp->fr_subtype))
	{
	  /* We generate a fixup instead of applying it right now
	     because, if there are linker relaxations, we're going to
	     need the relocations.  */
	  exp.X_op = O_symbol;
	  exp.X_add_symbol = fragp->fr_symbol;
	  exp.X_add_number = fragp->fr_offset;

	  fixp = fix_new_exp (fragp, buf - (bfd_byte *)fragp->fr_literal,
			      4, &exp, 1,
			      BFD_RELOC_16_PCREL_S2);
	  fixp->fx_file = fragp->fr_file;
	  fixp->fx_line = fragp->fr_line;

	  md_number_to_chars (buf, insn, 4);
	  buf += 4;
	}
      else
	{
	  int i;

	  as_warn_where (fragp->fr_file, fragp->fr_line,
			 _("relaxed out-of-range branch into a jump"));

	  if (RELAX_BRANCH_UNCOND (fragp->fr_subtype))
	    goto uncond;

	  if (!RELAX_BRANCH_LIKELY (fragp->fr_subtype))
	    {
	      /* Reverse the branch.  */
	      switch ((insn >> 28) & 0xf)
		{
		case 4:
		  /* bc[0-3][tf]l? and bc1any[24][ft] instructions can
		     have the condition reversed by tweaking a single
		     bit, and their opcodes all have 0x4???????.  */
		  assert ((insn & 0xf1000000) == 0x41000000);
		  insn ^= 0x00010000;
		  break;

		case 0:
		  /* bltz	0x04000000	bgez	0x04010000
		     bltzal	0x04100000	bgezal	0x04110000 */
		  assert ((insn & 0xfc0e0000) == 0x04000000);
		  insn ^= 0x00010000;
		  break;

		case 1:
		  /* beq	0x10000000	bne	0x14000000
		     blez	0x18000000	bgtz	0x1c000000 */
		  insn ^= 0x04000000;
		  break;

		default:
		  abort ();
		}
	    }

	  if (RELAX_BRANCH_LINK (fragp->fr_subtype))
	    {
	      /* Clear the and-link bit.  */
	      assert ((insn & 0xfc1c0000) == 0x04100000);

	      /* bltzal	0x04100000	bgezal	0x04110000
		bltzall	0x04120000     bgezall	0x04130000 */
	      insn &= ~0x00100000;
	    }

	  /* Branch over the branch (if the branch was likely) or the
	     full jump (not likely case).  Compute the offset from the
	     current instruction to branch to.  */
	  if (RELAX_BRANCH_LIKELY (fragp->fr_subtype))
	    i = 16;
	  else
	    {
	      /* How many bytes in instructions we've already emitted?  */
	      i = buf - (bfd_byte *)fragp->fr_literal - fragp->fr_fix;
	      /* How many bytes in instructions from here to the end?  */
	      i = fragp->fr_var - i;
	    }
	  /* Convert to instruction count.  */
	  i >>= 2;
	  /* Branch counts from the next instruction.  */
	  i--;
	  insn |= i;
	  /* Branch over the jump.  */
	  md_number_to_chars (buf, insn, 4);
	  buf += 4;

	  /* Nop */
	  md_number_to_chars (buf, 0, 4);
	  buf += 4;

	  if (RELAX_BRANCH_LIKELY (fragp->fr_subtype))
	    {
	      /* beql $0, $0, 2f */
	      insn = 0x50000000;
	      /* Compute the PC offset from the current instruction to
		 the end of the variable frag.  */
	      /* How many bytes in instructions we've already emitted?  */
	      i = buf - (bfd_byte *)fragp->fr_literal - fragp->fr_fix;
	      /* How many bytes in instructions from here to the end?  */
	      i = fragp->fr_var - i;
	      /* Convert to instruction count.  */
	      i >>= 2;
	      /* Don't decrement i, because we want to branch over the
		 delay slot.  */

	      insn |= i;
	      md_number_to_chars (buf, insn, 4);
	      buf += 4;

	      md_number_to_chars (buf, 0, 4);
	      buf += 4;
	    }

	uncond:
	  if (mips_pic == NO_PIC)
	    {
	      /* j or jal.  */
	      insn = (RELAX_BRANCH_LINK (fragp->fr_subtype)
		      ? 0x0c000000 : 0x08000000);
	      exp.X_op = O_symbol;
	      exp.X_add_symbol = fragp->fr_symbol;
	      exp.X_add_number = fragp->fr_offset;

	      fixp = fix_new_exp (fragp, buf - (bfd_byte *)fragp->fr_literal,
				  4, &exp, 0, BFD_RELOC_MIPS_JMP);
	      fixp->fx_file = fragp->fr_file;
	      fixp->fx_line = fragp->fr_line;

	      md_number_to_chars (buf, insn, 4);
	      buf += 4;
	    }
	  else
	    {
	      /* lw/ld $at, <sym>($gp)  R_MIPS_GOT16 */
	      insn = HAVE_64BIT_ADDRESSES ? 0xdf810000 : 0x8f810000;
	      exp.X_op = O_symbol;
	      exp.X_add_symbol = fragp->fr_symbol;
	      exp.X_add_number = fragp->fr_offset;

	      if (fragp->fr_offset)
		{
		  exp.X_add_symbol = make_expr_symbol (&exp);
		  exp.X_add_number = 0;
		}

	      fixp = fix_new_exp (fragp, buf - (bfd_byte *)fragp->fr_literal,
				  4, &exp, 0, BFD_RELOC_MIPS_GOT16);
	      fixp->fx_file = fragp->fr_file;
	      fixp->fx_line = fragp->fr_line;

	      md_number_to_chars (buf, insn, 4);
	      buf += 4;

	      if (mips_opts.isa == ISA_MIPS1)
		{
		  /* nop */
		  md_number_to_chars (buf, 0, 4);
		  buf += 4;
		}

	      /* d/addiu $at, $at, <sym>  R_MIPS_LO16 */
	      insn = HAVE_64BIT_ADDRESSES ? 0x64210000 : 0x24210000;

	      fixp = fix_new_exp (fragp, buf - (bfd_byte *)fragp->fr_literal,
				  4, &exp, 0, BFD_RELOC_LO16);
	      fixp->fx_file = fragp->fr_file;
	      fixp->fx_line = fragp->fr_line;

	      md_number_to_chars (buf, insn, 4);
	      buf += 4;

	      /* j(al)r $at.  */
	      if (RELAX_BRANCH_LINK (fragp->fr_subtype))
		insn = 0x0020f809;
	      else
		insn = 0x00200008;

	      md_number_to_chars (buf, insn, 4);
	      buf += 4;
	    }
	}

      assert (buf == (bfd_byte *)fragp->fr_literal
	      + fragp->fr_fix + fragp->fr_var);

      fragp->fr_fix += fragp->fr_var;

      return;
    }

  if (RELAX_MIPS16_P (fragp->fr_subtype))
    {
      int type;
      register const struct mips16_immed_operand *op;
      bfd_boolean small, ext;
      offsetT val;
      bfd_byte *buf;
      unsigned long insn;
      bfd_boolean use_extend;
      unsigned short extend;

      type = RELAX_MIPS16_TYPE (fragp->fr_subtype);
      op = mips16_immed_operands;
      while (op->type != type)
	++op;

      if (RELAX_MIPS16_EXTENDED (fragp->fr_subtype))
	{
	  small = FALSE;
	  ext = TRUE;
	}
      else
	{
	  small = TRUE;
	  ext = FALSE;
	}

      resolve_symbol_value (fragp->fr_symbol);
      val = S_GET_VALUE (fragp->fr_symbol);
      if (op->pcrel)
	{
	  addressT addr;

	  addr = fragp->fr_address + fragp->fr_fix;

	  /* The rules for the base address of a PC relative reloc are
             complicated; see mips16_extended_frag.  */
	  if (type == 'p' || type == 'q')
	    {
	      addr += 2;
	      if (ext)
		addr += 2;
	      /* Ignore the low bit in the target, since it will be
                 set for a text label.  */
	      if ((val & 1) != 0)
		--val;
	    }
	  else if (RELAX_MIPS16_JAL_DSLOT (fragp->fr_subtype))
	    addr -= 4;
	  else if (RELAX_MIPS16_DSLOT (fragp->fr_subtype))
	    addr -= 2;

	  addr &= ~ (addressT) ((1 << op->shift) - 1);
	  val -= addr;

	  /* Make sure the section winds up with the alignment we have
             assumed.  */
	  if (op->shift > 0)
	    record_alignment (asec, op->shift);
	}

      if (ext
	  && (RELAX_MIPS16_JAL_DSLOT (fragp->fr_subtype)
	      || RELAX_MIPS16_DSLOT (fragp->fr_subtype)))
	as_warn_where (fragp->fr_file, fragp->fr_line,
		       _("extended instruction in delay slot"));

      buf = (bfd_byte *) (fragp->fr_literal + fragp->fr_fix);

      if (target_big_endian)
	insn = bfd_getb16 (buf);
      else
	insn = bfd_getl16 (buf);

      mips16_immed (fragp->fr_file, fragp->fr_line, type, val,
		    RELAX_MIPS16_USER_EXT (fragp->fr_subtype),
		    small, ext, &insn, &use_extend, &extend);

      if (use_extend)
	{
	  md_number_to_chars (buf, 0xf000 | extend, 2);
	  fragp->fr_fix += 2;
	  buf += 2;
	}

      md_number_to_chars (buf, insn, 2);
      fragp->fr_fix += 2;
      buf += 2;
    }
  else
    {
      int first, second;
      fixS *fixp;

      first = RELAX_FIRST (fragp->fr_subtype);
      second = RELAX_SECOND (fragp->fr_subtype);
      fixp = (fixS *) fragp->fr_opcode;

      /* Possibly emit a warning if we've chosen the longer option.  */
      if (((fragp->fr_subtype & RELAX_USE_SECOND) != 0)
	  == ((fragp->fr_subtype & RELAX_SECOND_LONGER) != 0))
	{
	  const char *msg = macro_warning (fragp->fr_subtype);
	  if (msg != 0)
	    as_warn_where (fragp->fr_file, fragp->fr_line, msg);
	}

      /* Go through all the fixups for the first sequence.  Disable them
	 (by marking them as done) if we're going to use the second
	 sequence instead.  */
      while (fixp
	     && fixp->fx_frag == fragp
	     && fixp->fx_where < fragp->fr_fix - second)
	{
	  if (fragp->fr_subtype & RELAX_USE_SECOND)
	    fixp->fx_done = 1;
	  fixp = fixp->fx_next;
	}

      /* Go through the fixups for the second sequence.  Disable them if
	 we're going to use the first sequence, otherwise adjust their
	 addresses to account for the relaxation.  */
      while (fixp && fixp->fx_frag == fragp)
	{
	  if (fragp->fr_subtype & RELAX_USE_SECOND)
	    fixp->fx_where -= first;
	  else
	    fixp->fx_done = 1;
	  fixp = fixp->fx_next;
	}

      /* Now modify the frag contents.  */
      if (fragp->fr_subtype & RELAX_USE_SECOND)
	{
	  char *start;

	  start = fragp->fr_literal + fragp->fr_fix - first - second;
	  memmove (start, start + first, second);
	  fragp->fr_fix -= first;
	}
      else
	fragp->fr_fix -= second;
    }
}

#ifdef OBJ_ELF

/* This function is called after the relocs have been generated.
   We've been storing mips16 text labels as odd.  Here we convert them
   back to even for the convenience of the debugger.  */

void
mips_frob_file_after_relocs (void)
{
  asymbol **syms;
  unsigned int count, i;

  if (OUTPUT_FLAVOR != bfd_target_elf_flavour)
    return;

  syms = bfd_get_outsymbols (stdoutput);
  count = bfd_get_symcount (stdoutput);
  for (i = 0; i < count; i++, syms++)
    {
      if (elf_symbol (*syms)->internal_elf_sym.st_other == STO_MIPS16
	  && ((*syms)->value & 1) != 0)
	{
	  (*syms)->value &= ~1;
	  /* If the symbol has an odd size, it was probably computed
	     incorrectly, so adjust that as well.  */
	  if ((elf_symbol (*syms)->internal_elf_sym.st_size & 1) != 0)
	    ++elf_symbol (*syms)->internal_elf_sym.st_size;
	}
    }
}

#endif

/* This function is called whenever a label is defined.  It is used
   when handling branch delays; if a branch has a label, we assume we
   can not move it.  */

void
mips_define_label (symbolS *sym)
{
  struct insn_label_list *l;

  if (free_insn_labels == NULL)
    l = (struct insn_label_list *) xmalloc (sizeof *l);
  else
    {
      l = free_insn_labels;
      free_insn_labels = l->next;
    }

  l->label = sym;
  l->next = insn_labels;
  insn_labels = l;
}

#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)

/* Some special processing for a MIPS ELF file.  */

void
mips_elf_final_processing (void)
{
  /* Write out the register information.  */
  if (mips_abi != N64_ABI)
    {
      Elf32_RegInfo s;

      s.ri_gprmask = mips_gprmask;
      s.ri_cprmask[0] = mips_cprmask[0];
      s.ri_cprmask[1] = mips_cprmask[1];
      s.ri_cprmask[2] = mips_cprmask[2];
      s.ri_cprmask[3] = mips_cprmask[3];
      /* The gp_value field is set by the MIPS ELF backend.  */

      bfd_mips_elf32_swap_reginfo_out (stdoutput, &s,
				       ((Elf32_External_RegInfo *)
					mips_regmask_frag));
    }
  else
    {
      Elf64_Internal_RegInfo s;

      s.ri_gprmask = mips_gprmask;
      s.ri_pad = 0;
      s.ri_cprmask[0] = mips_cprmask[0];
      s.ri_cprmask[1] = mips_cprmask[1];
      s.ri_cprmask[2] = mips_cprmask[2];
      s.ri_cprmask[3] = mips_cprmask[3];
      /* The gp_value field is set by the MIPS ELF backend.  */

      bfd_mips_elf64_swap_reginfo_out (stdoutput, &s,
				       ((Elf64_External_RegInfo *)
					mips_regmask_frag));
    }

  /* Set the MIPS ELF flag bits.  FIXME: There should probably be some
     sort of BFD interface for this.  */
  if (mips_any_noreorder)
    elf_elfheader (stdoutput)->e_flags |= EF_MIPS_NOREORDER;
  if (mips_pic != NO_PIC)
    {
    elf_elfheader (stdoutput)->e_flags |= EF_MIPS_PIC;
      elf_elfheader (stdoutput)->e_flags |= EF_MIPS_CPIC;
    }
  if (mips_abicalls)
    elf_elfheader (stdoutput)->e_flags |= EF_MIPS_CPIC;

  /* Set MIPS ELF flags for ASEs.  */
  if (file_ase_mips16)
    elf_elfheader (stdoutput)->e_flags |= EF_MIPS_ARCH_ASE_M16;
#if 0 /* XXX FIXME */
  if (file_ase_mips3d)
    elf_elfheader (stdoutput)->e_flags |= ???;
#endif
  if (file_ase_mdmx)
    elf_elfheader (stdoutput)->e_flags |= EF_MIPS_ARCH_ASE_MDMX;

  /* Set the MIPS ELF ABI flags.  */
  if (mips_abi == O32_ABI && USE_E_MIPS_ABI_O32)
    elf_elfheader (stdoutput)->e_flags |= E_MIPS_ABI_O32;
  else if (mips_abi == O64_ABI)
    elf_elfheader (stdoutput)->e_flags |= E_MIPS_ABI_O64;
  else if (mips_abi == EABI_ABI)
    {
      if (!file_mips_gp32)
	elf_elfheader (stdoutput)->e_flags |= E_MIPS_ABI_EABI64;
      else
	elf_elfheader (stdoutput)->e_flags |= E_MIPS_ABI_EABI32;
    }
  else if (mips_abi == N32_ABI)
    elf_elfheader (stdoutput)->e_flags |= EF_MIPS_ABI2;

  /* Nothing to do for N64_ABI.  */

  if (mips_32bitmode)
    elf_elfheader (stdoutput)->e_flags |= EF_MIPS_32BITMODE;
}

#endif /* OBJ_ELF || OBJ_MAYBE_ELF */

typedef struct proc {
  symbolS *isym;
  unsigned long reg_mask;
  unsigned long reg_offset;
  unsigned long fpreg_mask;
  unsigned long fpreg_offset;
  unsigned long frame_offset;
  unsigned long frame_reg;
  unsigned long pc_reg;
} procS;

static procS cur_proc;
static procS *cur_proc_ptr;
static int numprocs;

/* Fill in an rs_align_code fragment.  */

void
mips_handle_align (fragS *fragp)
{
  if (fragp->fr_type != rs_align_code)
    return;

  if (mips_opts.mips16)
    {
      static const unsigned char be_nop[] = { 0x65, 0x00 };
      static const unsigned char le_nop[] = { 0x00, 0x65 };

      int bytes;
      char *p;

      bytes = fragp->fr_next->fr_address - fragp->fr_address - fragp->fr_fix;
      p = fragp->fr_literal + fragp->fr_fix;

      if (bytes & 1)
	{
	  *p++ = 0;
	  fragp->fr_fix++;
	}

      memcpy (p, (target_big_endian ? be_nop : le_nop), 2);
      fragp->fr_var = 2;
    }

  /* For mips32, a nop is a zero, which we trivially get by doing nothing.  */
}

static void
md_obj_begin (void)
{
}

static void
md_obj_end (void)
{
  /* check for premature end, nesting errors, etc */
  if (cur_proc_ptr)
    as_warn (_("missing .end at end of assembly"));
}

static long
get_number (void)
{
  int negative = 0;
  long val = 0;

  if (*input_line_pointer == '-')
    {
      ++input_line_pointer;
      negative = 1;
    }
  if (!ISDIGIT (*input_line_pointer))
    as_bad (_("expected simple number"));
  if (input_line_pointer[0] == '0')
    {
      if (input_line_pointer[1] == 'x')
	{
	  input_line_pointer += 2;
	  while (ISXDIGIT (*input_line_pointer))
	    {
	      val <<= 4;
	      val |= hex_value (*input_line_pointer++);
	    }
	  return negative ? -val : val;
	}
      else
	{
	  ++input_line_pointer;
	  while (ISDIGIT (*input_line_pointer))
	    {
	      val <<= 3;
	      val |= *input_line_pointer++ - '0';
	    }
	  return negative ? -val : val;
	}
    }
  if (!ISDIGIT (*input_line_pointer))
    {
      printf (_(" *input_line_pointer == '%c' 0x%02x\n"),
	      *input_line_pointer, *input_line_pointer);
      as_warn (_("invalid number"));
      return -1;
    }
  while (ISDIGIT (*input_line_pointer))
    {
      val *= 10;
      val += *input_line_pointer++ - '0';
    }
  return negative ? -val : val;
}

/* The .file directive; just like the usual .file directive, but there
   is an initial number which is the ECOFF file index.  In the non-ECOFF
   case .file implies DWARF-2.  */

static void
s_mips_file (int x ATTRIBUTE_UNUSED)
{
  static int first_file_directive = 0;

  if (ECOFF_DEBUGGING)
    {
      get_number ();
      s_app_file (0);
    }
  else
    {
      char *filename;

      filename = dwarf2_directive_file (0);

      /* Versions of GCC up to 3.1 start files with a ".file"
	 directive even for stabs output.  Make sure that this
	 ".file" is handled.  Note that you need a version of GCC
         after 3.1 in order to support DWARF-2 on MIPS.  */
      if (filename != NULL && ! first_file_directive)
	{
	  (void) new_logical_line (filename, -1);
	  s_app_file_string (filename);
	}
      first_file_directive = 1;
    }
}

/* The .loc directive, implying DWARF-2.  */

static void
s_mips_loc (int x ATTRIBUTE_UNUSED)
{
  if (!ECOFF_DEBUGGING)
    dwarf2_directive_loc (0);
}

/* The .end directive.  */

static void
s_mips_end (int x ATTRIBUTE_UNUSED)
{
  symbolS *p;

  /* Following functions need their own .frame and .cprestore directives.  */
  mips_frame_reg_valid = 0;
  mips_cprestore_valid = 0;

  if (!is_end_of_line[(unsigned char) *input_line_pointer])
    {
      p = get_symbol ();
      demand_empty_rest_of_line ();
    }
  else
    p = NULL;

  if ((bfd_get_section_flags (stdoutput, now_seg) & SEC_CODE) == 0)
    as_warn (_(".end not in text section"));

  if (!cur_proc_ptr)
    {
      as_warn (_(".end directive without a preceding .ent directive."));
      demand_empty_rest_of_line ();
      return;
    }

  if (p != NULL)
    {
      assert (S_GET_NAME (p));
      if (strcmp (S_GET_NAME (p), S_GET_NAME (cur_proc_ptr->isym)))
	as_warn (_(".end symbol does not match .ent symbol."));

      if (debug_type == DEBUG_STABS)
	stabs_generate_asm_endfunc (S_GET_NAME (p),
				    S_GET_NAME (p));
    }
  else
    as_warn (_(".end directive missing or unknown symbol"));

#ifdef OBJ_ELF
  /* Generate a .pdr section.  */
  if (OUTPUT_FLAVOR == bfd_target_elf_flavour && ! ECOFF_DEBUGGING
      && mips_flag_pdr)
    {
      segT saved_seg = now_seg;
      subsegT saved_subseg = now_subseg;
      valueT dot;
      expressionS exp;
      char *fragp;

      dot = frag_now_fix ();

#ifdef md_flush_pending_output
      md_flush_pending_output ();
#endif

      assert (pdr_seg);
      subseg_set (pdr_seg, 0);

      /* Write the symbol.  */
      exp.X_op = O_symbol;
      exp.X_add_symbol = p;
      exp.X_add_number = 0;
      emit_expr (&exp, 4);

      fragp = frag_more (7 * 4);

      md_number_to_chars (fragp, cur_proc_ptr->reg_mask, 4);
      md_number_to_chars (fragp + 4, cur_proc_ptr->reg_offset, 4);
      md_number_to_chars (fragp + 8, cur_proc_ptr->fpreg_mask, 4);
      md_number_to_chars (fragp + 12, cur_proc_ptr->fpreg_offset, 4);
      md_number_to_chars (fragp + 16, cur_proc_ptr->frame_offset, 4);
      md_number_to_chars (fragp + 20, cur_proc_ptr->frame_reg, 4);
      md_number_to_chars (fragp + 24, cur_proc_ptr->pc_reg, 4);

      subseg_set (saved_seg, saved_subseg);
    }
#endif /* OBJ_ELF */

  cur_proc_ptr = NULL;
}

/* The .aent and .ent directives.  */

static void
s_mips_ent (int aent)
{
  symbolS *symbolP;

  symbolP = get_symbol ();
  if (*input_line_pointer == ',')
    ++input_line_pointer;
  SKIP_WHITESPACE ();
  if (ISDIGIT (*input_line_pointer)
      || *input_line_pointer == '-')
    get_number ();

  if ((bfd_get_section_flags (stdoutput, now_seg) & SEC_CODE) == 0)
    as_warn (_(".ent or .aent not in text section."));

  if (!aent && cur_proc_ptr)
    as_warn (_("missing .end"));

  if (!aent)
    {
      /* This function needs its own .frame and .cprestore directives.  */
      mips_frame_reg_valid = 0;
      mips_cprestore_valid = 0;

      cur_proc_ptr = &cur_proc;
      memset (cur_proc_ptr, '\0', sizeof (procS));

      cur_proc_ptr->isym = symbolP;

      symbol_get_bfdsym (symbolP)->flags |= BSF_FUNCTION;

      ++numprocs;

      if (debug_type == DEBUG_STABS)
        stabs_generate_asm_func (S_GET_NAME (symbolP),
				 S_GET_NAME (symbolP));
    }

  demand_empty_rest_of_line ();
}

/* The .frame directive. If the mdebug section is present (IRIX 5 native)
   then ecoff.c (ecoff_directive_frame) is used. For embedded targets,
   s_mips_frame is used so that we can set the PDR information correctly.
   We can't use the ecoff routines because they make reference to the ecoff
   symbol table (in the mdebug section).  */

static void
s_mips_frame (int ignore ATTRIBUTE_UNUSED)
{
#ifdef OBJ_ELF
  if (OUTPUT_FLAVOR == bfd_target_elf_flavour && ! ECOFF_DEBUGGING)
    {
      long val;

      if (cur_proc_ptr == (procS *) NULL)
	{
	  as_warn (_(".frame outside of .ent"));
	  demand_empty_rest_of_line ();
	  return;
	}

      cur_proc_ptr->frame_reg = tc_get_register (1);

      SKIP_WHITESPACE ();
      if (*input_line_pointer++ != ','
	  || get_absolute_expression_and_terminator (&val) != ',')
	{
	  as_warn (_("Bad .frame directive"));
	  --input_line_pointer;
	  demand_empty_rest_of_line ();
	  return;
	}

      cur_proc_ptr->frame_offset = val;
      cur_proc_ptr->pc_reg = tc_get_register (0);

      demand_empty_rest_of_line ();
    }
  else
#endif /* OBJ_ELF */
    s_ignore (ignore);
}

/* The .fmask and .mask directives. If the mdebug section is present
   (IRIX 5 native) then ecoff.c (ecoff_directive_mask) is used. For
   embedded targets, s_mips_mask is used so that we can set the PDR
   information correctly. We can't use the ecoff routines because they
   make reference to the ecoff symbol table (in the mdebug section).  */

static void
s_mips_mask (int reg_type)
{
#ifdef OBJ_ELF
  if (OUTPUT_FLAVOR == bfd_target_elf_flavour && ! ECOFF_DEBUGGING)
    {
      long mask, off;

      if (cur_proc_ptr == (procS *) NULL)
	{
	  as_warn (_(".mask/.fmask outside of .ent"));
	  demand_empty_rest_of_line ();
	  return;
	}

      if (get_absolute_expression_and_terminator (&mask) != ',')
	{
	  as_warn (_("Bad .mask/.fmask directive"));
	  --input_line_pointer;
	  demand_empty_rest_of_line ();
	  return;
	}

      off = get_absolute_expression ();

      if (reg_type == 'F')
	{
	  cur_proc_ptr->fpreg_mask = mask;
	  cur_proc_ptr->fpreg_offset = off;
	}
      else
	{
	  cur_proc_ptr->reg_mask = mask;
	  cur_proc_ptr->reg_offset = off;
	}

      demand_empty_rest_of_line ();
    }
  else
#endif /* OBJ_ELF */
    s_ignore (reg_type);
}

/* The .loc directive.  */

#if 0
static void
s_loc (int x)
{
  symbolS *symbolP;
  int lineno;
  int addroff;

  assert (now_seg == text_section);

  lineno = get_number ();
  addroff = frag_now_fix ();

  symbolP = symbol_new ("", N_SLINE, addroff, frag_now);
  S_SET_TYPE (symbolP, N_SLINE);
  S_SET_OTHER (symbolP, 0);
  S_SET_DESC (symbolP, lineno);
  symbolP->sy_segment = now_seg;
}
#endif

/* A table describing all the processors gas knows about.  Names are
   matched in the order listed.

   To ease comparison, please keep this table in the same order as
   gcc's mips_cpu_info_table[].  */
static const struct mips_cpu_info mips_cpu_info_table[] =
{
  /* Entries for generic ISAs */
  { "mips1",          1,      ISA_MIPS1,      CPU_R3000 },
  { "mips2",          1,      ISA_MIPS2,      CPU_R6000 },
  { "mips3",          1,      ISA_MIPS3,      CPU_R4000 },
  { "mips4",          1,      ISA_MIPS4,      CPU_R8000 },
  { "mips5",          1,      ISA_MIPS5,      CPU_MIPS5 },
  { "mips32",         1,      ISA_MIPS32,     CPU_MIPS32 },
  { "mips32r2",       1,      ISA_MIPS32R2,   CPU_MIPS32R2 },
  { "mips64",         1,      ISA_MIPS64,     CPU_MIPS64 },
  { "mips64r2",       1,      ISA_MIPS64R2,   CPU_MIPS64R2 },

  /* MIPS I */
  { "r3000",          0,      ISA_MIPS1,      CPU_R3000 },
  { "r2000",          0,      ISA_MIPS1,      CPU_R3000 },
  { "r3900",          0,      ISA_MIPS1,      CPU_R3900 },

  /* MIPS II */
  { "r6000",          0,      ISA_MIPS2,      CPU_R6000 },

  /* MIPS III */
  { "r4000",          0,      ISA_MIPS3,      CPU_R4000 },
  { "r4010",          0,      ISA_MIPS2,      CPU_R4010 },
  { "vr4100",         0,      ISA_MIPS3,      CPU_VR4100 },
  { "vr4111",         0,      ISA_MIPS3,      CPU_R4111 },
  { "vr4120",         0,      ISA_MIPS3,      CPU_VR4120 },
  { "vr4130",         0,      ISA_MIPS3,      CPU_VR4120 },
  { "vr4181",         0,      ISA_MIPS3,      CPU_R4111 },
  { "vr4300",         0,      ISA_MIPS3,      CPU_R4300 },
  { "r4400",          0,      ISA_MIPS3,      CPU_R4400 },
  { "r4600",          0,      ISA_MIPS3,      CPU_R4600 },
  { "orion",          0,      ISA_MIPS3,      CPU_R4600 },
  { "r4650",          0,      ISA_MIPS3,      CPU_R4650 },

  /* MIPS IV */
  { "r8000",          0,      ISA_MIPS4,      CPU_R8000 },
  { "r10000",         0,      ISA_MIPS4,      CPU_R10000 },
  { "r12000",         0,      ISA_MIPS4,      CPU_R12000 },
  { "vr5000",         0,      ISA_MIPS4,      CPU_R5000 },
  { "vr5400",         0,      ISA_MIPS4,      CPU_VR5400 },
  { "vr5500",         0,      ISA_MIPS4,      CPU_VR5500 },
  { "rm5200",         0,      ISA_MIPS4,      CPU_R5000 },
  { "rm5230",         0,      ISA_MIPS4,      CPU_R5000 },
  { "rm5231",         0,      ISA_MIPS4,      CPU_R5000 },
  { "rm5261",         0,      ISA_MIPS4,      CPU_R5000 },
  { "rm5721",         0,      ISA_MIPS4,      CPU_R5000 },
  { "rm7000",         0,      ISA_MIPS4,      CPU_RM7000 },
  { "rm9000",         0,      ISA_MIPS4,      CPU_RM7000 },

  /* MIPS 32 */
  { "4kc",            0,      ISA_MIPS32,     CPU_MIPS32 },
  { "4km",            0,      ISA_MIPS32,     CPU_MIPS32 },
  { "4kp",            0,      ISA_MIPS32,     CPU_MIPS32 },

  /* MIPS 64 */
  { "5kc",            0,      ISA_MIPS64,     CPU_MIPS64 },
  { "20kc",           0,      ISA_MIPS64,     CPU_MIPS64 },

  /* Broadcom SB-1 CPU core */
  { "sb1",            0,      ISA_MIPS64,     CPU_SB1 },

  /* End marker */
  { NULL, 0, 0, 0 }
};


/* Return true if GIVEN is the same as CANONICAL, or if it is CANONICAL
   with a final "000" replaced by "k".  Ignore case.

   Note: this function is shared between GCC and GAS.  */

static bfd_boolean
mips_strict_matching_cpu_name_p (const char *canonical, const char *given)
{
  while (*given != 0 && TOLOWER (*given) == TOLOWER (*canonical))
    given++, canonical++;

  return ((*given == 0 && *canonical == 0)
	  || (strcmp (canonical, "000") == 0 && strcasecmp (given, "k") == 0));
}


/* Return true if GIVEN matches CANONICAL, where GIVEN is a user-supplied
   CPU name.  We've traditionally allowed a lot of variation here.

   Note: this function is shared between GCC and GAS.  */

static bfd_boolean
mips_matching_cpu_name_p (const char *canonical, const char *given)
{
  /* First see if the name matches exactly, or with a final "000"
     turned into "k".  */
  if (mips_strict_matching_cpu_name_p (canonical, given))
    return TRUE;

  /* If not, try comparing based on numerical designation alone.
     See if GIVEN is an unadorned number, or 'r' followed by a number.  */
  if (TOLOWER (*given) == 'r')
    given++;
  if (!ISDIGIT (*given))
    return FALSE;

  /* Skip over some well-known prefixes in the canonical name,
     hoping to find a number there too.  */
  if (TOLOWER (canonical[0]) == 'v' && TOLOWER (canonical[1]) == 'r')
    canonical += 2;
  else if (TOLOWER (canonical[0]) == 'r' && TOLOWER (canonical[1]) == 'm')
    canonical += 2;
  else if (TOLOWER (canonical[0]) == 'r')
    canonical += 1;

  return mips_strict_matching_cpu_name_p (canonical, given);
}


/* Parse an option that takes the name of a processor as its argument.
   OPTION is the name of the option and CPU_STRING is the argument.
   Return the corresponding processor enumeration if the CPU_STRING is
   recognized, otherwise report an error and return null.

   A similar function exists in GCC.  */

static const struct mips_cpu_info *
mips_parse_cpu (const char *option, const char *cpu_string)
{
  const struct mips_cpu_info *p;

  /* 'from-abi' selects the most compatible architecture for the given
     ABI: MIPS I for 32-bit ABIs and MIPS III for 64-bit ABIs.  For the
     EABIs, we have to decide whether we're using the 32-bit or 64-bit
     version.  Look first at the -mgp options, if given, otherwise base
     the choice on MIPS_DEFAULT_64BIT.

     Treat NO_ABI like the EABIs.  One reason to do this is that the
     plain 'mips' and 'mips64' configs have 'from-abi' as their default
     architecture.  This code picks MIPS I for 'mips' and MIPS III for
     'mips64', just as we did in the days before 'from-abi'.  */
  if (strcasecmp (cpu_string, "from-abi") == 0)
    {
      if (ABI_NEEDS_32BIT_REGS (mips_abi))
	return mips_cpu_info_from_isa (ISA_MIPS1);

      if (ABI_NEEDS_64BIT_REGS (mips_abi))
	return mips_cpu_info_from_isa (ISA_MIPS3);

      if (file_mips_gp32 >= 0)
	return mips_cpu_info_from_isa (file_mips_gp32 ? ISA_MIPS1 : ISA_MIPS3);

      return mips_cpu_info_from_isa (MIPS_DEFAULT_64BIT
				     ? ISA_MIPS3
				     : ISA_MIPS1);
    }

  /* 'default' has traditionally been a no-op.  Probably not very useful.  */
  if (strcasecmp (cpu_string, "default") == 0)
    return 0;

  for (p = mips_cpu_info_table; p->name != 0; p++)
    if (mips_matching_cpu_name_p (p->name, cpu_string))
      return p;

  as_bad ("Bad value (%s) for %s", cpu_string, option);
  return 0;
}

/* Return the canonical processor information for ISA (a member of the
   ISA_MIPS* enumeration).  */

static const struct mips_cpu_info *
mips_cpu_info_from_isa (int isa)
{
  int i;

  for (i = 0; mips_cpu_info_table[i].name != NULL; i++)
    if (mips_cpu_info_table[i].is_isa
	&& isa == mips_cpu_info_table[i].isa)
      return (&mips_cpu_info_table[i]);

  return NULL;
}

static const struct mips_cpu_info *
mips_cpu_info_from_arch (int arch)
{
  int i;

  for (i = 0; mips_cpu_info_table[i].name != NULL; i++)
    if (arch == mips_cpu_info_table[i].cpu)
      return (&mips_cpu_info_table[i]);

  return NULL;
}

static void
show (FILE *stream, const char *string, int *col_p, int *first_p)
{
  if (*first_p)
    {
      fprintf (stream, "%24s", "");
      *col_p = 24;
    }
  else
    {
      fprintf (stream, ", ");
      *col_p += 2;
    }

  if (*col_p + strlen (string) > 72)
    {
      fprintf (stream, "\n%24s", "");
      *col_p = 24;
    }

  fprintf (stream, "%s", string);
  *col_p += strlen (string);

  *first_p = 0;
}

void
md_show_usage (FILE *stream)
{
  int column, first;
  size_t i;

  fprintf (stream, _("\
MIPS options:\n\
-membedded-pic		generate embedded position independent code\n\
-EB			generate big endian output\n\
-EL			generate little endian output\n\
-g, -g2			do not remove unneeded NOPs or swap branches\n\
-G NUM			allow referencing objects up to NUM bytes\n\
			implicitly with the gp register [default 8]\n"));
  fprintf (stream, _("\
-mips1			generate MIPS ISA I instructions\n\
-mips2			generate MIPS ISA II instructions\n\
-mips3			generate MIPS ISA III instructions\n\
-mips4			generate MIPS ISA IV instructions\n\
-mips5                  generate MIPS ISA V instructions\n\
-mips32                 generate MIPS32 ISA instructions\n\
-mips32r2               generate MIPS32 release 2 ISA instructions\n\
-mips64                 generate MIPS64 ISA instructions\n\
-mips64r2               generate MIPS64 release 2 ISA instructions\n\
-march=CPU/-mtune=CPU	generate code/schedule for CPU, where CPU is one of:\n"));

  first = 1;

  for (i = 0; mips_cpu_info_table[i].name != NULL; i++)
    show (stream, mips_cpu_info_table[i].name, &column, &first);
  show (stream, "from-abi", &column, &first);
  fputc ('\n', stream);

  fprintf (stream, _("\
-mCPU			equivalent to -march=CPU -mtune=CPU. Deprecated.\n\
-no-mCPU		don't generate code specific to CPU.\n\
			For -mCPU and -no-mCPU, CPU must be one of:\n"));

  first = 1;

  show (stream, "3900", &column, &first);
  show (stream, "4010", &column, &first);
  show (stream, "4100", &column, &first);
  show (stream, "4650", &column, &first);
  fputc ('\n', stream);

  fprintf (stream, _("\
-mips16			generate mips16 instructions\n\
-no-mips16		do not generate mips16 instructions\n"));
  fprintf (stream, _("\
-mgp32			use 32-bit GPRs, regardless of the chosen ISA\n\
-mfp32			use 32-bit FPRs, regardless of the chosen ISA\n\
-O0			remove unneeded NOPs, do not swap branches\n\
-O			remove unneeded NOPs and swap branches\n\
--[no-]construct-floats [dis]allow floating point values to be constructed\n\
--trap, --no-break	trap exception on div by 0 and mult overflow\n\
--break, --no-trap	break exception on div by 0 and mult overflow\n"));
#ifdef OBJ_ELF
  fprintf (stream, _("\
-KPIC, -call_shared	generate SVR4 position independent code\n\
-non_shared		do not generate position independent code\n\
-xgot			assume a 32 bit GOT\n\
-mpdr, -mno-pdr		enable/disable creation of .pdr sections\n\
-mabi=ABI		create ABI conformant object file for:\n"));

  first = 1;

  show (stream, "32", &column, &first);
  show (stream, "o64", &column, &first);
  show (stream, "n32", &column, &first);
  show (stream, "64", &column, &first);
  show (stream, "eabi", &column, &first);

  fputc ('\n', stream);

  fprintf (stream, _("\
-32			create o32 ABI object file (default)\n\
-n32			create n32 ABI object file\n\
-64			create 64 ABI object file\n"));
#endif
}

enum dwarf2_format
mips_dwarf2_format (void)
{
  if (mips_abi == N64_ABI)
    {
#ifdef TE_IRIX
      return dwarf2_format_64bit_irix;
#else
      return dwarf2_format_64bit;
#endif
    }
  else
    return dwarf2_format_32bit;
}
