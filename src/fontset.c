/* Fontset handler.
   Copyright (C) 1995, 1997, 2000 Electrotechnical Laboratory, JAPAN.
   Licensed to the Free Software Foundation.

This file is part of GNU Emacs.

GNU Emacs is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU Emacs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Emacs; see the file COPYING.  If not, write to
the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

/* #define FONTSET_DEBUG */

#include <config.h>

#ifdef FONTSET_DEBUG
#include <stdio.h>
#endif

#include "lisp.h"
#include "charset.h"
#include "ccl.h"
#include "frame.h"
#include "dispextern.h"
#include "fontset.h"
#include "window.h"

#ifdef FONTSET_DEBUG
#undef xassert
#define xassert(X)	do {if (!(X)) abort ();} while (0)
#undef INLINE
#define INLINE
#endif


/* FONTSET

   A fontset is a collection of font related information to give
   similar appearance (style, size, etc) of characters.  There are two
   kinds of fontsets; base and realized.  A base fontset is created by
   new-fontset from Emacs Lisp explicitly.  A realized fontset is
   created implicitly when a face is realized for ASCII characters.  A
   face is also realized for multibyte characters based on an ASCII
   face.  All of the multibyte faces based on the same ASCII face
   share the same realized fontset.
   
   A fontset object is implemented by a char-table.

   An element of a base fontset is:
	(INDEX . FONTNAME) or
	(INDEX . (FOUNDRY . REGISTRY ))
   FONTNAME is a font name pattern for the corresponding character.
   FOUNDRY and REGISTRY are respectively foundy and regisry fields of
   a font name for the corresponding character.  INDEX specifies for
   which character (or generic character) the element is defined.  It
   may be different from an index to access this element.  For
   instance, if a fontset defines some font for all characters of
   charset `japanese-jisx0208', INDEX is the generic character of this
   charset.  REGISTRY is the

   An element of a realized fontset is FACE-ID which is a face to use
   for displaying the correspnding character.

   All single byte charaters (ASCII and 8bit-unibyte) share the same
   element in a fontset.  The element is stored in `defalt' slot of
   the fontset.  And this slot is never used as a default value of
   multibyte characters.  That means that the first 256 elements of a
   fontset set is always nil (as this is not efficient, we may
   implement a fontset in a different way in the future).

   To access or set each element, use macros FONTSET_REF and
   FONTSET_SET respectively for efficiency.

   A fontset has 3 extra slots.

   The 1st slot is an ID number of the fontset.

   The 2nd slot is a name of the fontset.  This is nil for a realized
   face.

   The 3rd slot is a frame that the fontset belongs to.  This is nil
   for a default face.

   A parent of a base fontset is nil.  A parent of a realized fontset
   is a base fontset.

   All fontsets (except for the default fontset described below) are
   recorded in Vfontset_table.


   DEFAULT FONTSET

   There's a special fontset named `default fontset' which defines a
   default fontname that contains only REGISTRY field for each
   character.  When a base fontset doesn't specify a font for a
   specific character, the corresponding value in the default fontset
   is used.  The format is the same as a base fontset.

   The parent of realized fontsets created for faces that have no
   fontset is the default fontset.


   These structures are hidden from the other codes than this file.
   The other codes handle fontsets only by their ID numbers.  They
   usually use variable name `fontset' for IDs.  But, in this file, we
   always use varialbe name `id' for IDs, and name `fontset' for the
   actual fontset objects.

*/

/********** VARIABLES and FUNCTION PROTOTYPES **********/

extern Lisp_Object Qfont;
Lisp_Object Qfontset;

/* Vector containing all fontsets.  */
static Lisp_Object Vfontset_table;

/* Next possibly free fontset ID.  Usually this keeps the mininum
   fontset ID not yet used.  */
static int next_fontset_id;

/* The default fontset.  This gives default FAMILY and REGISTRY of
   font for each characters.  */
static Lisp_Object Vdefault_fontset;

Lisp_Object Vfont_encoding_alist;
Lisp_Object Vuse_default_ascent;
Lisp_Object Vignore_relative_composition;
Lisp_Object Valternate_fontname_alist;
Lisp_Object Vfontset_alias_alist;
Lisp_Object Vhighlight_wrong_size_font;
Lisp_Object Vclip_large_size_font;
Lisp_Object Vvertical_centering_font_regexp;

/* The following six are declarations of callback functions depending
   on window system.  See the comments in src/fontset.h for more
   detail.  */

/* Return a pointer to struct font_info of font FONT_IDX of frame F.  */
struct font_info *(*get_font_info_func) P_ ((FRAME_PTR f, int font_idx));

/* Return a list of font names which matches PATTERN.  See the document of
   `x-list-fonts' for more detail.  */
Lisp_Object (*list_fonts_func) P_ ((struct frame *f,
				    Lisp_Object pattern,
				    int size,
				    int maxnames));

/* Load a font named NAME for frame F and return a pointer to the
   information of the loaded font.  If loading is failed, return 0.  */
struct font_info *(*load_font_func) P_ ((FRAME_PTR f, char *name, int));

/* Return a pointer to struct font_info of a font named NAME for frame F.  */
struct font_info *(*query_font_func) P_ ((FRAME_PTR f, char *name));

/* Additional function for setting fontset or changing fontset
   contents of frame F.  */
void (*set_frame_fontset_func) P_ ((FRAME_PTR f, Lisp_Object arg,
				    Lisp_Object oldval));

/* To find a CCL program, fs_load_font calls this function.
   The argument is a pointer to the struct font_info.
   This function set the memer `encoder' of the structure.  */
void (*find_ccl_program_func) P_ ((struct font_info *));

/* Check if any window system is used now.  */
void (*check_window_system_func) P_ ((void));


/* Prototype declarations for static functions.  */
static Lisp_Object fontset_ref P_ ((Lisp_Object, int));
static void fontset_set P_ ((Lisp_Object, int, Lisp_Object));
static Lisp_Object make_fontset P_ ((Lisp_Object, Lisp_Object, Lisp_Object));
static int fontset_id_valid_p P_ ((int));
static Lisp_Object fontset_pattern_regexp P_ ((Lisp_Object));
static Lisp_Object font_family_registry P_ ((Lisp_Object));


/********** MACROS AND FUNCTIONS TO HANDLE FONTSET **********/

/* Macros for Lisp vector.  */
#define AREF(V, IDX)	XVECTOR (V)->contents[IDX]
#define ASIZE(V)	XVECTOR (V)->size

/* Return the fontset with ID.  No check of ID's validness.  */
#define FONTSET_FROM_ID(id) AREF (Vfontset_table, id)

/* Macros to access extra, default, and parent slots, of fontset.  */
#define FONTSET_ID(fontset)		XCHAR_TABLE (fontset)->extras[0]
#define FONTSET_NAME(fontset)		XCHAR_TABLE (fontset)->extras[1]
#define FONTSET_FRAME(fontset)		XCHAR_TABLE (fontset)->extras[2]
#define FONTSET_ASCII(fontset)		XCHAR_TABLE (fontset)->defalt
#define FONTSET_BASE(fontset)		XCHAR_TABLE (fontset)->parent

#define BASE_FONTSET_P(fontset)		NILP (FONTSET_BASE(fontset))


/* Return the element of FONTSET (char-table) at index C (character).  */

#define FONTSET_REF(fontset, c)	fontset_ref (fontset, c)

static INLINE Lisp_Object
fontset_ref (fontset, c)
     Lisp_Object fontset;
     int c;
{
  int charset, c1, c2;
  Lisp_Object elt, defalt;
  int i;

  if (SINGLE_BYTE_CHAR_P (c))
    return FONTSET_ASCII (fontset);

  SPLIT_NON_ASCII_CHAR (c, charset, c1, c2);
  elt = XCHAR_TABLE (fontset)->contents[charset + 128];
  if (!SUB_CHAR_TABLE_P (elt))
    return elt;
  defalt = XCHAR_TABLE (elt)->defalt;
  if (c1 < 32
      || (elt = XCHAR_TABLE (elt)->contents[c1],
	  NILP (elt)))
    return defalt;
  if (!SUB_CHAR_TABLE_P (elt))
    return elt;
  defalt = XCHAR_TABLE (elt)->defalt;
  if (c2 < 32
      || (elt = XCHAR_TABLE (elt)->contents[c2],
	  NILP (elt)))
    return defalt;
  return elt;
}


#define FONTSET_REF_VIA_BASE(fontset, c) fontset_ref_via_base (fontset, &c)

static INLINE Lisp_Object
fontset_ref_via_base (fontset, c)
     Lisp_Object fontset;
     int *c;
{
  int charset, c1, c2;
  Lisp_Object elt;
  int i;

  if (SINGLE_BYTE_CHAR_P (*c))
    return FONTSET_ASCII (fontset);

  elt = FONTSET_REF (FONTSET_BASE (fontset), *c);
  if (NILP (elt))
    return Qnil;

  *c = XINT (XCAR (elt));
  SPLIT_NON_ASCII_CHAR (*c, charset, c1, c2);
  elt = XCHAR_TABLE (fontset)->contents[charset + 128];
  if (c1 < 32)
    return (SUB_CHAR_TABLE_P (elt) ? XCHAR_TABLE (elt)->defalt : elt);
  if (!SUB_CHAR_TABLE_P (elt))
    return Qnil;
  elt = XCHAR_TABLE (elt)->contents[c1];
  if (c2 < 32)
    return (SUB_CHAR_TABLE_P (elt) ? XCHAR_TABLE (elt)->defalt : elt);
  if (!SUB_CHAR_TABLE_P (elt))
    return Qnil;
  elt = XCHAR_TABLE (elt)->contents[c2];
  return elt;
}


/* Store into the element of FONTSET at index C the value NEWETL.  */
#define FONTSET_SET(fontset, c, newelt) fontset_set(fontset, c, newelt)

static void
fontset_set (fontset, c, newelt)
     Lisp_Object fontset;
     int c;
     Lisp_Object newelt;
{
  int charset, code[3];
  Lisp_Object *elt, tmp;
  int i, j;

  if (SINGLE_BYTE_CHAR_P (c))
    {
      FONTSET_ASCII (fontset) = newelt;
      return;
    }

  SPLIT_NON_ASCII_CHAR (c, charset, code[0], code[1]);
  code[2] = 0;			/* anchor */
  elt = &XCHAR_TABLE (fontset)->contents[charset + 128];
  for (i = 0; code[i] > 0; i++)
    {
      if (!SUB_CHAR_TABLE_P (*elt))
	*elt = make_sub_char_table (*elt);
      elt = &XCHAR_TABLE (*elt)->contents[code[i]];
    }
  if (SUB_CHAR_TABLE_P (*elt))
    XCHAR_TABLE (*elt)->defalt = newelt;
  else
    *elt = newelt;
}


/* Return a newly created fontset with NAME.  If BASE is nil, make a
   base fontset.  Otherwise make a realized fontset whose parent is
   BASE.  */

static Lisp_Object
make_fontset (frame, name, base)
     Lisp_Object frame, name, base;
{
  Lisp_Object fontset, elt, base_elt;
  int size = ASIZE (Vfontset_table);
  int id = next_fontset_id;
  int i, j;

  /* Find a free slot in Vfontset_table.  Usually, next_fontset_id is
     the next available fontset ID.  So it is expected that this loop
     terminates quickly.  In addition, as the last element of
     Vfotnset_table is always nil, we don't have to check the range of
     id.  */
  while (!NILP (AREF (Vfontset_table, id))) id++;

  if (id + 1 == size)
    {
      Lisp_Object tem;
      int i; 

      tem = Fmake_vector (make_number (size + 8), Qnil);
      for (i = 0; i < size; i++)
	AREF (tem, i) = AREF (Vfontset_table, i);
      Vfontset_table = tem;
    }

  if (NILP (base))
    fontset = Fcopy_sequence (Vdefault_fontset);
  else
    fontset = Fmake_char_table (Qfontset, Qnil);

  FONTSET_ID (fontset) = make_number (id);
  FONTSET_NAME (fontset) = name;
  FONTSET_FRAME (fontset) = frame;
  FONTSET_BASE (fontset) = base;

  AREF (Vfontset_table, id) = fontset;
  next_fontset_id = id + 1;
  return fontset;
}


/* Return 1 if ID is a valid fontset id, else return 0.  */

static INLINE int
fontset_id_valid_p (id)
     int id;
{
  return (id >= 0 && id < ASIZE (Vfontset_table) - 1);
}


/* Extract `family' and `registry' string from FONTNAME and set in
   *FAMILY and *REGISTRY respectively.  Actually, `family' may also
   contain `foundry', `registry' may also contain `encoding' of
   FONTNAME.  */

static Lisp_Object
font_family_registry (fontname)
     Lisp_Object fontname;
{
  Lisp_Object family, registry;
  char *p = XSTRING (fontname)->data;
  char *sep[15];
  int i = 0;
  
  while (*p && i < 15) if (*p++ == '-') sep[i++] = p;
  if (i != 14)
    return fontname;

  family = make_unibyte_string (sep[0], sep[2] - 1 - sep[0]);
  registry = make_unibyte_string (sep[12], p - sep[12]);
  return Fcons (family, registry);
}


/********** INTERFACES TO xfaces.c and dispextern.h **********/ 

/* Return name of the fontset with ID.  */

Lisp_Object
fontset_name (id)
     int id;
{
  Lisp_Object fontset;
  fontset = FONTSET_FROM_ID (id);
  return FONTSET_NAME (fontset);
}


/* Return ASCII font name of the fontset with ID.  */

Lisp_Object
fontset_ascii (id)
     int id;
{
  Lisp_Object fontset, elt;
  fontset= FONTSET_FROM_ID (id);
  elt = FONTSET_ASCII (fontset);
  return XCDR (elt);
}


/* Free fontset of FACE.  Called from free_realized_face.  */

void
free_face_fontset (f, face)
     FRAME_PTR f;
     struct face *face;
{
  if (fontset_id_valid_p (face->fontset))
    {
      AREF (Vfontset_table, face->fontset) = Qnil;
      if (face->fontset < next_fontset_id)
	next_fontset_id = face->fontset;
    }
}


/* Return 1 iff FACE is suitable for displaying character C.
   Otherwise return 0.  Called from the macro FACE_SUITABLE_FOR_CHAR_P
   when C is not a single byte character..  */

int
face_suitable_for_char_p (face, c)
     struct face *face;
     int c;
{
  Lisp_Object fontset, elt;

  if (SINGLE_BYTE_CHAR_P (c))
    return (face == face->ascii_face);

  xassert (fontset_id_valid_p (face->fontset));
  fontset = FONTSET_FROM_ID (face->fontset);
  xassert (!BASE_FONTSET_P (fontset));

  elt = FONTSET_REF_VIA_BASE (fontset, c);
  return (!NILP (elt) && face->id == XFASTINT (elt));
}


/* Return ID of face suitable for displaying character C on frame F.
   The selection of face is done based on the fontset of FACE.  FACE
   should already have been realized for ASCII characters.  Called
   from the macro FACE_FOR_CHAR when C is not a single byte character.  */

int
face_for_char (f, face, c)
     FRAME_PTR f;
     struct face *face;
     int c;
{
  Lisp_Object fontset, elt;
  int face_id;

  xassert (fontset_id_valid_p (face->fontset));
  fontset = FONTSET_FROM_ID (face->fontset);
  xassert (!BASE_FONTSET_P (fontset));

  elt = FONTSET_REF_VIA_BASE (fontset, c);
  if (!NILP (elt))
    return XINT (elt);

  /* No face is recorded for C in the fontset of FACE.  Make a new
     realized face for C that has the same fontset.  */
  face_id = lookup_face (f, face->lface, c, face);
  
  /* Record the face ID in FONTSET at the same index as the
     information in the base fontset.  */
  FONTSET_SET (fontset, c, make_number (face_id));
  return face_id;
}


/* Make a realized fontset for ASCII face FACE on frame F from the
   base fontset BASE_FONTSET_ID.  If BASE_FONTSET_ID is -1, use the
   default fontset as the base.  Value is the id of the new fontset.
   Called from realize_x_face.  */

int
make_fontset_for_ascii_face (f, base_fontset_id)
     FRAME_PTR f;
     int base_fontset_id;
{
  Lisp_Object base_fontset, fontset, name, frame;

  XSETFRAME (frame, f);
  if (base_fontset_id >= 0)
    {
      base_fontset = FONTSET_FROM_ID (base_fontset_id);
      if (!BASE_FONTSET_P (base_fontset))
	base_fontset = FONTSET_BASE (base_fontset);
      xassert (BASE_FONTSET_P (base_fontset));
    }
  else
    base_fontset = Vdefault_fontset;

  fontset = make_fontset (frame, Qnil, base_fontset);
  return XINT (FONTSET_ID (fontset));
}


/* Return the font name pattern for C that is recorded in the fontset
   with ID.  A font is opened by that pattern to get the fullname.  If
   the fullname conform to XLFD, extract foundry-family field and
   registry-encoding field, and return the cons of them.  Otherwise
   return the fullname.  If ID is -1, or the fontset doesn't contain
   information about C, get the registry and encoding of C from the
   default fontset.  Called from choose_face_font.  */

Lisp_Object
fontset_font_pattern (f, id, c)
     FRAME_PTR f;
     int id, c;
{
  Lisp_Object fontset, elt;
  struct font_info *fontp;
  Lisp_Object family_registry;
  
  elt = Qnil;
  if (fontset_id_valid_p (id))
    {
      fontset = FONTSET_FROM_ID (id);
      xassert (!BASE_FONTSET_P (fontset));
      fontset = FONTSET_BASE (fontset);
      elt = FONTSET_REF (fontset, c);
    }
  else
    elt = FONTSET_REF (Vdefault_fontset, c);

  if (!CONSP (elt))
    return Qnil;
  if (CONSP (XCDR (elt)))
    return XCDR (elt);

  /* The fontset specifies only a font name pattern (not cons of
     family and registry).  Try to open a font by that pattern and get
     a registry from the full name of the opened font.  We ignore
     family name here because it should be wild card in the fontset
     specification.  */
  elt = XCDR (elt);
  xassert (STRINGP (elt));
  fontp = FS_LOAD_FONT (f, c, XSTRING (elt)->data, -1);
  if (!fontp)
    return Qnil;

  family_registry = font_family_registry (build_string (fontp->full_name));
  if (!CONSP (family_registry))
    return family_registry;
  XCAR (family_registry) = Qnil;
  return family_registry;
}


/* Load a font named FONTNAME to display character C on frame F.
   Return a pointer to the struct font_info of the loaded font.  If
   loading fails, return NULL.  If FACE is non-zero and a fontset is
   assigned to it, record FACE->id in the fontset for C.  If FONTNAME
   is NULL, the name is taken from the fontset of FACE or what
   specified by ID.  */

struct font_info *
fs_load_font (f, c, fontname, id, face)
     FRAME_PTR f;
     int c;
     char *fontname;
     int id;
     struct face *face;
{
  Lisp_Object fontset;
  Lisp_Object list, elt;
  int font_idx;
  int size = 0;
  struct font_info *fontp;
  int charset = CHAR_CHARSET (c);

  if (face)
    id = face->fontset;
  if (id < 0)
    fontset = Qnil;
  else
    fontset = FONTSET_FROM_ID (id);

  if (!NILP (fontset)
      && !BASE_FONTSET_P (fontset))
    {
      elt = FONTSET_REF_VIA_BASE (fontset, c);
      if (!NILP (elt))
	{
	  /* A suitable face for C is already recorded, which means
	     that a proper font is already loaded.  */
	  int face_id = XINT (elt);

	  xassert (face_id == face->id);
	  face = FACE_FROM_ID (f, face_id);
	  return (*get_font_info_func) (f, face->font_info_id);
	}

      if (!fontname && charset == CHARSET_ASCII)
	{
	  elt = FONTSET_ASCII (fontset);
	  fontname = XSTRING (XCDR (elt))->data;
	}
    }

  if (!fontname)
    /* No way to get fontname.  */
    return 0;

  fontp = (*load_font_func) (f, fontname, size);
  if (!fontp)
    return 0;

  /* Fill in members (charset, vertical_centering, encoding, etc) of
     font_info structure that are not set by (*load_font_func).  */
  fontp->charset = charset;

  fontp->vertical_centering
    = (STRINGP (Vvertical_centering_font_regexp)
       && (fast_c_string_match_ignore_case 
	   (Vvertical_centering_font_regexp, fontp->full_name) >= 0));

  if (fontp->encoding[1] != FONT_ENCODING_NOT_DECIDED)
    {
      /* The font itself tells which code points to be used.  Use this
	 encoding for all other charsets.  */
      int i;

      fontp->encoding[0] = fontp->encoding[1];
      for (i = MIN_CHARSET_OFFICIAL_DIMENSION1; i <= MAX_CHARSET; i++)
	fontp->encoding[i] = fontp->encoding[1];
    }
  else
    {
      /* The font itself doesn't have information about encoding.  */
      int i;

      /* At first, set 1 (means 0xA0..0xFF) as the default.  */
      fontp->encoding[0] = 1;
      for (i = MIN_CHARSET_OFFICIAL_DIMENSION1; i <= MAX_CHARSET; i++)
	fontp->encoding[i] = 1;
      /* Then override them by a specification in Vfont_encoding_alist.  */
      for (list = Vfont_encoding_alist; CONSP (list); list = XCDR (list))
	{
	  elt = XCAR (list);
	  if (CONSP (elt)
	      && STRINGP (XCAR (elt)) && CONSP (XCDR (elt))
	      && (fast_c_string_match_ignore_case (XCAR (elt), fontname)
		  >= 0))
	    {
	      Lisp_Object tmp;

	      for (tmp = XCDR (elt); CONSP (tmp); tmp = XCDR (tmp))
		if (CONSP (XCAR (tmp))
		    && ((i = get_charset_id (XCAR (XCAR (tmp))))
			>= 0)
		    && INTEGERP (XCDR (XCAR (tmp)))
		    && XFASTINT (XCDR (XCAR (tmp))) < 4)
		  fontp->encoding[i]
		    = XFASTINT (XCDR (XCAR (tmp)));
	    }
	}
    }

  fontp->font_encoder = (struct ccl_program *) 0;

  if (find_ccl_program_func)
    (*find_ccl_program_func) (fontp);

  return fontp;
}


/* Cache data used by fontset_pattern_regexp.  The car part is a
   pattern string containing at least one wild card, the cdr part is
   the corresponding regular expression.  */
static Lisp_Object Vcached_fontset_data;

#define CACHED_FONTSET_NAME (XSTRING (XCAR (Vcached_fontset_data))->data)
#define CACHED_FONTSET_REGEX (XCDR (Vcached_fontset_data))

/* If fontset name PATTERN contains any wild card, return regular
   expression corresponding to PATTERN.  */

static Lisp_Object
fontset_pattern_regexp (pattern)
     Lisp_Object pattern;
{
  if (!index (XSTRING (pattern)->data, '*')
      && !index (XSTRING (pattern)->data, '?'))
    /* PATTERN does not contain any wild cards.  */
    return Qnil;

  if (!CONSP (Vcached_fontset_data)
      || strcmp (XSTRING (pattern)->data, CACHED_FONTSET_NAME))
    {
      /* We must at first update the cached data.  */
      char *regex = (char *) alloca (XSTRING (pattern)->size * 2);
      char *p0, *p1 = regex;

      /* Convert "*" to ".*", "?" to ".".  */
      *p1++ = '^';
      for (p0 = (char *) XSTRING (pattern)->data; *p0; p0++)
	{
	  if (*p0 == '*')
	    {
	      *p1++ = '.';
	      *p1++ = '*';
	    }
	  else if (*p0 == '?')
	    *p1++ = '.';
	  else
	    *p1++ = *p0;
	}
      *p1++ = '$';
      *p1++ = 0;

      Vcached_fontset_data = Fcons (build_string (XSTRING (pattern)->data),
				    build_string (regex));
    }

  return CACHED_FONTSET_REGEX;
}

/* Return ID of the base fontset named NAME.  If there's no such
   fontset, return -1.  */

int
fs_query_fontset (name, regexpp)
     Lisp_Object name;
     int regexpp;
{
  Lisp_Object fontset, tem;
  int i;

  name = Fdowncase (name);
  if (!regexpp)
    {
      tem = Frassoc (name, Vfontset_alias_alist);
      if (CONSP (tem) && STRINGP (XCAR (tem)))
	name = XCAR (tem);
      else
	{
	  tem = fontset_pattern_regexp (name);
	  if (STRINGP (tem))
	    {
	      name = tem;
	      regexpp = 1;
	    }
	}
    }

  for (i = 0; i < ASIZE (Vfontset_table); i++)
    {
      Lisp_Object fontset;
      unsigned char *this_name;

      fontset = FONTSET_FROM_ID (i);
      if (NILP (fontset)
	  || !BASE_FONTSET_P (fontset))
	continue;

      this_name = XSTRING (FONTSET_NAME (fontset))->data;
      if (regexpp
	  ? fast_c_string_match_ignore_case (name, this_name) >= 0
	  : !strcmp (XSTRING (name)->data, this_name))
	return i;
    }
  return -1;
}


DEFUN ("query-fontset", Fquery_fontset, Squery_fontset, 1, 2, 0,
  "Return the name of a fontset that matches PATTERN.\n\
The value is nil if there is no matching fontset.\n\
PATTERN can contain `*' or `?' as a wildcard\n\
just as X font name matching algorithm allows.\n\
If REGEXPP is non-nil, PATTERN is a regular expression.")
  (pattern, regexpp)
     Lisp_Object pattern, regexpp;
{
  Lisp_Object fontset;
  int id;

  (*check_window_system_func) ();

  CHECK_STRING (pattern, 0);

  if (XSTRING (pattern)->size == 0)
    return Qnil;

  id = fs_query_fontset (pattern, !NILP (regexpp));
  if (id < 0)
    return Qnil;

  fontset = FONTSET_FROM_ID (id);
  return FONTSET_NAME (fontset);
}

/* Return a list of base fontset names matching PATTERN on frame F.
   If SIZE is not 0, it is the size (maximum bound width) of fontsets
   to be listed. */

Lisp_Object
list_fontsets (f, pattern, size)
     FRAME_PTR f;
     Lisp_Object pattern;
     int size;
{
  Lisp_Object frame, regexp, val, tail;
  int id;

  XSETFRAME (frame, f);

  regexp = fontset_pattern_regexp (pattern);
  val = Qnil;

  for (id = 0; id < ASIZE (Vfontset_table); id++)
    {
      Lisp_Object fontset;
      unsigned char *name;

      fontset = FONTSET_FROM_ID (id);
      if (NILP (fontset)
	  || !BASE_FONTSET_P (fontset)
	  || !EQ (frame, FONTSET_FRAME (fontset)))
	continue;
      name = XSTRING (FONTSET_NAME (fontset))->data;

      if (!NILP (regexp)
	  ? (fast_c_string_match_ignore_case (regexp, name) < 0)
	  : strcmp (XSTRING (pattern)->data, name))
	continue;

      if (size)
	{
	  struct font_info *fontp;
	  fontp = FS_LOAD_FONT (f, 0, NULL, id);
	  if (!fontp || size != fontp->size)
	    continue;
	}
      val = Fcons (Fcopy_sequence (FONTSET_NAME (fontset)), val);
    }

  return val;
}

DEFUN ("new-fontset", Fnew_fontset, Snew_fontset, 2, 2, 0,
  "Create a new fontset NAME that contains font information in FONTLIST.\n\
FONTLIST is an alist of charsets vs corresponding font name patterns.")
  (name, fontlist)
     Lisp_Object name, fontlist;
{
  Lisp_Object fontset, elements, ascii_font;
  Lisp_Object tem, tail, elt;

  (*check_window_system_func) ();

  CHECK_STRING (name, 0);
  CHECK_LIST (fontlist, 1);

  name = Fdowncase (name);
  tem = Fquery_fontset (name, Qnil);
  if (!NILP (tem))
    error ("Fontset `%s' matches the existing fontset `%s'",
	   XSTRING (name)->data, XSTRING (tem)->data);

  /* Check the validity of FONTLIST while creating a template for
     fontset elements.  */
  elements = ascii_font = Qnil;
  for (tail = fontlist; CONSP (tail); tail = XCDR (tail))
    {
      Lisp_Object family, registry;
      int c, charset;

      tem = XCAR (tail);
      if (!CONSP (tem)
	  || (charset = get_charset_id (XCAR (tem))) < 0
	  || !STRINGP (XCDR (tem)))
	error ("Elements of fontlist must be a cons of charset and font name");

      tem = Fdowncase (XCDR (tem));
      if (charset == CHARSET_ASCII)
	ascii_font = tem;
      else
	{
	  c = MAKE_CHAR (charset, 0, 0);
	  elements = Fcons (Fcons (make_number (c), tem), elements);
	}
    }

  if (NILP (ascii_font))
    error ("No ASCII font in the fontlist");

  fontset = make_fontset (Qnil, name, Qnil);
  FONTSET_ASCII (fontset) = Fcons (make_number (0), ascii_font);
  for (; CONSP (elements); elements = XCDR (elements))
    {
      elt = XCAR (elements);
      tem = Fcons (XCAR (elt), font_family_registry (XCDR (elt)));
      FONTSET_SET (fontset, XINT (XCAR (elt)), tem);
    }

  return Qnil;
}


/* Clear all elements of FONTSET for multibyte characters.  */

static void
clear_fontset_elements (fontset)
     Lisp_Object fontset;
{
  int i;

  for (i = CHAR_TABLE_SINGLE_BYTE_SLOTS; i < CHAR_TABLE_ORDINARY_SLOTS; i++)
    XCHAR_TABLE (fontset)->contents[i] = Qnil;
}


/* Return 1 iff REGISTRY is a valid string as the font registry and
   encoding.  It is valid if it doesn't start with `-' and the number
   of `-' in the string is at most 1.  */

static int
check_registry_encoding (registry)
     Lisp_Object registry;
{
  unsigned char *str = XSTRING (registry)->data;
  unsigned char *p = str;
  int i;

  if (!*p || *p++ == '-')
    return 0;
  for (i = 0; *p; p++)
    if (*p == '-') i++;
  return (i < 2);
}


/* Check validity of NAME as a fontset name and return the
   corresponding fontset.  If not valid, signal an error.
   If NAME is t, return Vdefault_fontset.  */

static Lisp_Object
check_fontset_name (name)
     Lisp_Object name;
{
  int id;

  if (EQ (name, Qt))
    return Vdefault_fontset;

  CHECK_STRING (name, 0);
  id = fs_query_fontset (name, 0);
  if (id < 0)
    error ("Fontset `%s' does not exist", XSTRING (name)->data);
  return FONTSET_FROM_ID (id);
}

DEFUN ("set-fontset-font", Fset_fontset_font, Sset_fontset_font, 3, 4, 0,
  "Modify fontset NAME to use FONTNAME for character CHAR.\n\
\n\
CHAR may be a cons; (FROM . TO), where FROM and TO are\n\
non-generic characters.  In that case, use FONTNAME\n\
for all characters in the range FROM and TO (inclusive).\n\
\n\
If NAME is t, an entry in the default fontset is modified.\n\
In that case, FONTNAME should be a registry and encoding name\n\
of a font for CHAR.")
  (name, ch, fontname, frame)
     Lisp_Object name, ch, fontname, frame;
{
  Lisp_Object fontset, elt;
  Lisp_Object realized;
  int from, to;
  int id;

  fontset = check_fontset_name (name);

  if (CONSP (ch))
    {
      /* CH should be (FROM . TO) where FROM and TO are non-generic
	 characters.  */
      CHECK_NUMBER (XCAR (ch), 1);
      CHECK_NUMBER (XCDR (ch), 1);
      from = XINT (XCAR (ch));
      to = XINT (XCDR (ch));
      if (!char_valid_p (from, 0) || !char_valid_p (to, 0))
	error ("Character range should be by non-generic characters.");
      if (!NILP (name)
	  && (SINGLE_BYTE_CHAR_P (from) || SINGLE_BYTE_CHAR_P (to)))
	error ("Can't change font for a single byte character");
    }
  else
    {
      CHECK_NUMBER (ch, 1);
      from = XINT (ch);
      to = from;
    }
  if (!char_valid_p (from, 1))
    invalid_character (from);
  if (SINGLE_BYTE_CHAR_P (from))
    error ("Can't change font for a single byte character");
  if (from < to)
    {
      if (!char_valid_p (to, 1))
	invalid_character (to);
      if (SINGLE_BYTE_CHAR_P (to))
	error ("Can't change font for a single byte character");
    }

  CHECK_STRING (fontname, 2);
  fontname = Fdowncase (fontname);
  if (EQ (fontset, Vdefault_fontset))
    {
      if (!check_registry_encoding (fontname))
	error ("Invalid registry and encoding name: %s",
	       XSTRING (fontname)->data);
      elt = Fcons (make_number (from), Fcons (Qnil, fontname));
    }
  else
    elt = Fcons (make_number (from), font_family_registry (fontname));

  /* The arg FRAME is kept for backward compatibility.  We only check
     the validity.  */
  if (!NILP (frame))
    CHECK_LIVE_FRAME (frame, 3);

  for (; from <= to; from++)
    FONTSET_SET (fontset, from, elt);
  Foptimize_char_table (fontset);

  /* If there's a realized fontset REALIZED whose parent is FONTSET,
     clear all the elements of REALIZED and free all multibyte faces
     whose fontset is REALIZED.  This way, the specified character(s)
     are surely redisplayed by a correct font.  */
  for (id = 0; id < ASIZE (Vfontset_table); id++)
    {
      realized = AREF (Vfontset_table, id);
      if (!NILP (realized)
	  && !BASE_FONTSET_P (realized)
	  && EQ (FONTSET_BASE (realized), fontset))
	{
	  FRAME_PTR f = XFRAME (FONTSET_FRAME (realized));
	  clear_fontset_elements (realized);
	  free_realized_multibyte_face (f, id);
	}
    }

  return Qnil;
}

DEFUN ("font-info", Ffont_info, Sfont_info, 1, 2, 0,
  "Return information about a font named NAME on frame FRAME.\n\
If FRAME is omitted or nil, use the selected frame.\n\
The returned value is a vector of OPENED-NAME, FULL-NAME, CHARSET, SIZE,\n\
  HEIGHT, BASELINE-OFFSET, RELATIVE-COMPOSE, and DEFAULT-ASCENT,\n\
where\n\
  OPENED-NAME is the name used for opening the font,\n\
  FULL-NAME is the full name of the font,\n\
  SIZE is the maximum bound width of the font,\n\
  HEIGHT is the height of the font,\n\
  BASELINE-OFFSET is the upward offset pixels from ASCII baseline,\n\
  RELATIVE-COMPOSE and DEFAULT-ASCENT are the numbers controlling\n\
    how to compose characters.\n\
If the named font is not yet loaded, return nil.")
  (name, frame)
     Lisp_Object name, frame;
{
  FRAME_PTR f;
  struct font_info *fontp;
  Lisp_Object info;

  (*check_window_system_func) ();

  CHECK_STRING (name, 0);
  name = Fdowncase (name);
  if (NILP (frame))
    frame = selected_frame;
  CHECK_LIVE_FRAME (frame, 1);
  f = XFRAME (frame);

  if (!query_font_func)
    error ("Font query function is not supported");

  fontp = (*query_font_func) (f, XSTRING (name)->data);
  if (!fontp)
    return Qnil;

  info = Fmake_vector (make_number (7), Qnil);

  XVECTOR (info)->contents[0] = build_string (fontp->name);
  XVECTOR (info)->contents[1] = build_string (fontp->full_name);
  XVECTOR (info)->contents[2] = make_number (fontp->size);
  XVECTOR (info)->contents[3] = make_number (fontp->height);
  XVECTOR (info)->contents[4] = make_number (fontp->baseline_offset);
  XVECTOR (info)->contents[5] = make_number (fontp->relative_compose);
  XVECTOR (info)->contents[6] = make_number (fontp->default_ascent);

  return info;
}

DEFUN ("fontset-info", Ffontset_info, Sfontset_info, 1, 2, 0,
  "Return information about a fontset named NAME on frame FRAME.\n\
If FRAME is omitted or nil, use the selected frame.\n\
The returned value is a vector of SIZE, HEIGHT, and FONT-LIST,\n\
where\n\
  SIZE is the maximum bound width of ASCII font of the fontset,\n\
  HEIGHT is the height of the ASCII font in the fontset, and\n\
  FONT-LIST is an alist of the format:\n\
    (CHARSET REQUESTED-FONT-NAME LOADED-FONT-NAME).\n\
LOADED-FONT-NAME t means the font is not yet loaded, nil means the\n\
loading failed.")
  (name, frame)
     Lisp_Object name, frame;
{
  FRAME_PTR f;
  Lisp_Object fontset, realized;
  Lisp_Object info, val, loaded, requested;
  int i;
  
  (*check_window_system_func) ();

  fontset = check_fontset_name (name);

  if (NILP (frame))
    frame = selected_frame;
  CHECK_LIVE_FRAME (frame, 1);
  f = XFRAME (frame);

  info = Fmake_vector (make_number (3), Qnil);

  for (i = 0; i < ASIZE (Vfontset_table); i++)
    {
      realized = FONTSET_FROM_ID (i);
      if (!NILP (realized)
	  && EQ (FONTSET_FRAME (realized), frame)
	  && EQ (FONTSET_BASE (realized), fontset)
	  && INTEGERP (FONTSET_ASCII (realized)))
	break;
    }

  if (NILP (realized))
    return Qnil;

  XVECTOR (info)->contents[0] = Qnil;
  XVECTOR (info)->contents[1] = Qnil;
  loaded = Qnil;

  val = Fcons (Fcons (CHARSET_SYMBOL (CHARSET_ASCII),
		      Fcons (FONTSET_ASCII (fontset),
			     Fcons (loaded, Qnil))),
	       Qnil);
  for (i = MIN_CHARSET_OFFICIAL_DIMENSION1; i <= MAX_CHARSET; i++)
    {
      Lisp_Object elt;
      elt = XCHAR_TABLE (fontset)->contents[i + 128];

      if (VECTORP (elt))
	{
	  int face_id;
	  struct face *face;

	  if (INTEGERP (AREF (elt, 2))
	      && (face_id = XINT (AREF (elt, 2)),
		  face = FACE_FROM_ID (f, face_id)))
	    {
	      struct font_info *fontp;
	      fontp = (*get_font_info_func) (f, face->font_info_id);
   	      requested = build_string (fontp->name);
	      loaded = (fontp->full_name
			? build_string (fontp->full_name)
			: Qnil);
	    }
	  else
	    {
	      char *str;
	      int family_len = 0, registry_len = 0;

	      if (STRINGP (AREF (elt, 0)))
		family_len = STRING_BYTES (XSTRING (AREF (elt, 0)));
	      if (STRINGP (AREF (elt, 1)))
		registry_len = STRING_BYTES (XSTRING (AREF (elt, 1)));
	      str = (char *) alloca (1 + family_len + 3 + registry_len + 1);
	      str[0] = '-';
	      str[1] = 0;
	      if (family_len)
		strcat (str, XSTRING (AREF (elt, 0))->data);
	      strcat (str, "-*-");
	      if (registry_len)
   		strcat (str, XSTRING (AREF (elt, 1))->data);
	      requested = build_string (str);
	      loaded = Qnil;
	    }
	  val = Fcons (Fcons (CHARSET_SYMBOL (i),
			      Fcons (requested, Fcons (loaded, Qnil))),
		       val);
	}
    }
  XVECTOR (info)->contents[2] = val;
  return info;
}

DEFUN ("fontset-font", Ffontset_font, Sfontset_font, 2, 2, 0,
  "Return a font name pattern for character CH in fontset NAME.\n\
If NAME is t, find a font name pattern in the default fontset.")
  (name, ch)
     Lisp_Object name, ch;
{
  int c, id;
  Lisp_Object fontset, elt;

  fontset = check_fontset_name (name);

  CHECK_NUMBER (ch, 1);
  c = XINT (ch);
  if (!char_valid_p (c, 1))
    invalid_character (c);

  elt = FONTSET_REF (fontset, c);
  if (CONSP (elt))
    elt = XCDR (elt);

  return elt;
}
  

DEFUN ("fontset-list", Ffontset_list, Sfontset_list, 0, 0, 0,
  "Return a list of all defined fontset names.")
  ()
{
  Lisp_Object fontset, list;
  int i;

  list = Qnil;
  for (i = 0; i < ASIZE (Vfontset_table); i++)
    {
      fontset = FONTSET_FROM_ID (i);
      if (!NILP (fontset)
	  && BASE_FONTSET_P (fontset))
	list = Fcons (FONTSET_NAME (fontset), list);
    }
  return list;
}

void
syms_of_fontset ()
{
  int i;

  if (!load_font_func)
    /* Window system initializer should have set proper functions.  */
    abort ();

  Qfontset = intern ("fontset");
  staticpro (&Qfontset);
  Fput (Qfontset, Qchar_table_extra_slots, make_number (3));

  Vcached_fontset_data = Qnil;
  staticpro (&Vcached_fontset_data);

  Vfontset_table = Fmake_vector (make_number (32), Qnil);
  staticpro (&Vfontset_table);
  next_fontset_id = 0;

  Vdefault_fontset = Fmake_char_table (Qfontset, Qnil);
  staticpro (&Vdefault_fontset);
  FONTSET_ASCII (Vdefault_fontset)
    = Fcons (make_number (0), Fcons (Qnil, build_string ("iso8859-1")));

  DEFVAR_LISP ("font-encoding-alist", &Vfont_encoding_alist,
    "Alist of fontname patterns vs corresponding encoding info.\n\
Each element looks like (REGEXP . ENCODING-INFO),\n\
 where ENCODING-INFO is an alist of CHARSET vs ENCODING.\n\
ENCODING is one of the following integer values:\n\
	0: code points 0x20..0x7F or 0x2020..0x7F7F are used,\n\
	1: code points 0xA0..0xFF or 0xA0A0..0xFFFF are used,\n\
	2: code points 0x20A0..0x7FFF are used,\n\
	3: code points 0xA020..0xFF7F are used.");
  Vfont_encoding_alist = Qnil;

  DEFVAR_LISP ("use-default-ascent", &Vuse_default_ascent,
     "Char table of characters whose ascent values should be ignored.\n\
If an entry for a character is non-nil, the ascent value of the glyph\n\
is assumed to be what specified by _MULE_DEFAULT_ASCENT property of a font.\n\
\n\
This affects how a composite character which contains\n\
such a character is displayed on screen.");
  Vuse_default_ascent = Qnil;

  DEFVAR_LISP ("ignore-relative-composition", &Vignore_relative_composition,
     "Char table of characters which is not composed relatively.\n\
If an entry for a character is non-nil, a composition sequence\n\
which contains that character is displayed so that\n\
the glyph of that character is put without considering\n\
an ascent and descent value of a previous character.");
  Vignore_relative_composition = Qnil;

  DEFVAR_LISP ("alternate-fontname-alist", &Valternate_fontname_alist,
     "Alist of fontname vs list of the alternate fontnames.\n\
When a specified font name is not found, the corresponding\n\
alternate fontnames (if any) are tried instead.");
  Valternate_fontname_alist = Qnil;

  DEFVAR_LISP ("fontset-alias-alist", &Vfontset_alias_alist,
     "Alist of fontset names vs the aliases.");
  Vfontset_alias_alist = Qnil;

  DEFVAR_LISP ("highlight-wrong-size-font", &Vhighlight_wrong_size_font,
     "*Non-nil means highlight characters shown in wrong size fonts somehow.\n\
The way to highlight them depends on window system on which Emacs runs.\n\
On X11, a rectangle is shown around each such character.");
  Vhighlight_wrong_size_font = Qnil;

  DEFVAR_LISP ("clip-large-size-font", &Vclip_large_size_font,
     "*Non-nil means characters shown in overlarge fonts are clipped.\n\
The height of clipping area is the same as that of an ASCII character.\n\
The width of the area is the same as that of an ASCII character,\n\
or twice as wide, depending on the character set's column-width.\n\
\n\
If the only font you have for a specific character set is too large,\n\
and clipping these characters makes them hard to read,\n\
you can set this variable to nil to display the characters without clipping.\n\
The drawback is that you will get some garbage left on your screen.");
  Vclip_large_size_font = Qt;

  DEFVAR_LISP ("vertical-centering-font-regexp",
	       &Vvertical_centering_font_regexp,
    "*Regexp matching font names that require vertical centering on display.\n\
When a character is displayed with such fonts, the character is displayed\n\
at the vertival center of lines.");
  Vvertical_centering_font_regexp = Qnil;

  defsubr (&Squery_fontset);
  defsubr (&Snew_fontset);
  defsubr (&Sset_fontset_font);
  defsubr (&Sfont_info);
  defsubr (&Sfontset_info);
  defsubr (&Sfontset_font);
  defsubr (&Sfontset_list);
}
