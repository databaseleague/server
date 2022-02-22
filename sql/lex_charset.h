#ifndef LEX_CHARSET_INCLUDED
#define LEX_CHARSET_INCLUDED

/* Copyright (c) 2021, MariaDB Corporation.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335  USA */

#include "mysys_charset.h"   // Charset_loader_mysys

/*
  Parse time character set and collation.

  Can be:

  1. Empty (not specified on the column level):
     CREATE TABLE t1 (a CHAR(10)) CHARACTER SET latin2;        -- (1a)
     CREATE TABLE t1 (a CHAR(10));                             -- (1b)

  2. Precisely typed:
     CREATE TABLE t1 (a CHAR(10) COLLATE latin1_bin);          -- (2a)
     CREATE TABLE t1 (
       a CHAR(10) CHARACTER SET latin1 COLLATE latin1_bin);    -- (2b)

  3. Contextually typed:
     CREATE TABLE t2 (a CHAR(10) BINARY) CHARACTER SET latin2; -- (3a)
     CREATE TABLE t2 (a CHAR(10) BINARY);                      -- (3b)
     CREATE TABLE t2 (a CHAR(10) COLLATE DEFAULT)
       CHARACER SET latin2 COLLATE latin2_bin;                 -- (3c)

  In case of an empty or a contextually typed collation,
  it is a subject to later resolution, when the context
  character set becomes known in the end of the CREATE statement:
  - either after the explicit table level CHARACTER SET, like in (1a,3a,3c)
  - or by the inhereted database level CHARACTER SET, like in (1b,3b)

  Resolution happens in Type_handler::Column_definition_prepare_stage1().
*/
struct Lex_charset_collation_st
{
public:
  enum Type
  {
    TYPE_EMPTY= 0,
    TYPE_CHARACTER_SET= 1,
    TYPE_COLLATE_EXACT= 2,
    TYPE_COLLATE_CONTEXTUALLY_TYPED= 3
  };
  static constexpr uint TYPE_LAST= TYPE_COLLATE_CONTEXTUALLY_TYPED;
protected:
  CHARSET_INFO *m_ci;
  Type m_type;

  static void
  error_conflicting_collations_or_styles(const Lex_charset_collation_st &a,
                                         const Lex_charset_collation_st &b);
public:

  static Lex_charset_collation_st get_by_name_or_error(const char *name,
                                                       myf utf8_flag);
  void init()
  {
    m_ci= NULL;
    m_type= TYPE_EMPTY;
  }
  bool is_empty() const
  {
    return m_type == TYPE_EMPTY;
  }
  void set_charset(CHARSET_INFO *cs)
  {
    DBUG_ASSERT(cs);
    m_ci= cs;
    m_type= TYPE_CHARACTER_SET;
  }
  bool set_charset_collate_default(CHARSET_INFO *cs)
  {
    DBUG_ASSERT(cs);
    if (!(cs->state & MY_CS_PRIMARY) &&
        !(cs= Charset_loader_mysys().find_default_collation(cs)))
      return true;
    m_ci= cs;
    m_type= TYPE_COLLATE_EXACT;
    return false;
  }
  bool set_charset_collate_binary(CHARSET_INFO *cs)
  {
    DBUG_ASSERT(cs);
    if (!(cs= Charset_loader_mysys().find_bin_collation_or_error(cs)))
      return true;
    m_ci= cs;
    m_type= TYPE_COLLATE_EXACT;
    return false;
  }
  bool set_charset_collate_exact(CHARSET_INFO *cs,
                                 CHARSET_INFO *cl);
  void set_collate_default()
  {
    m_ci= &my_charset_latin1;
    m_type= TYPE_COLLATE_CONTEXTUALLY_TYPED;
  }
  void set_contextually_typed_binary_style()
  {
    m_ci= &my_charset_bin;
    m_type= TYPE_COLLATE_CONTEXTUALLY_TYPED;
  }
  bool is_contextually_typed_collate_default() const
  {
    return m_ci == &my_charset_latin1 &&
           m_type == TYPE_COLLATE_CONTEXTUALLY_TYPED;
  }
  bool is_contextually_typed_binary_style() const
  {
    return m_ci == &my_charset_bin &&
           m_type == TYPE_COLLATE_CONTEXTUALLY_TYPED;
  }
  void set_collate_exact(CHARSET_INFO *cl)
  {
    DBUG_ASSERT(cl);
    m_ci= cl;
    m_type= TYPE_COLLATE_EXACT;
  }
  void set_collate_contextually_typed(CHARSET_INFO *cl)
  {
    DBUG_ASSERT(cl);
    m_ci= cl;
    m_type= TYPE_COLLATE_CONTEXTUALLY_TYPED;
  }
  CHARSET_INFO *charset_collation() const
  {
    return m_ci;
  }
  Type type() const
  {
    return m_type;
  }
  bool is_contextually_typed_collation() const
  {
    return m_type == TYPE_COLLATE_CONTEXTUALLY_TYPED;
  }
  /*
    Skip the character set prefix, return the suffix.
      utf8mb4_uca1400_as_ci -> uca1400_as_ci
  */
  LEX_CSTRING collation_name_context_suffix() const
  {
    return m_ci->get_collation_name(MY_COLLATION_NAME_MODE_CONTEXT);
  }
  LEX_CSTRING collation_name_for_show() const;
  CHARSET_INFO *resolved_to_character_set(CHARSET_INFO *cs) const;
  bool merge_charset_clause_and_collate_clause(const Lex_charset_collation_st &cl);
  bool merge_collate_clause_and_collate_clause(const Lex_charset_collation_st &cl);
  bool merge_unordered_charset_exact(CHARSET_INFO *cs);
  bool merge_unordered_collate_clause(const Lex_charset_collation_st &cl);
};


/*
  An extension for Lex_charset_collation_st.
  Additionally supports CHARACTER SET DEFAULT.
*/
class Lex_maybe_default_charset_collation_st: public Lex_charset_collation_st
{
protected:
  bool m_had_charset_default;
public:
  void init()
  {
    Lex_charset_collation_st::init();
    m_had_charset_default= false;
  }
  CHARSET_INFO *resolved_to_character_set(CHARSET_INFO *upper_level_default,
                                          CHARSET_INFO *current_level_default)
                                          const;
  bool merge_unordered_charset_default();
  bool merge_unordered_charset_exact(CHARSET_INFO *cs);
  bool merge_unordered_collate_clause(const Lex_charset_collation_st &cl);
};


/*
  CHARACTER SET cs [COLLATE cl]
*/
class Lex_explicit_charset_opt_collate
{
  CHARSET_INFO *m_ci;
  bool m_with_collate;
public:
  Lex_explicit_charset_opt_collate(CHARSET_INFO *ci, bool with_collate)
   :m_ci(ci), m_with_collate(with_collate)
  {
    DBUG_ASSERT(m_ci);
    // Item_func_set_collation uses non-default collations in "ci"
    //DBUG_ASSERT(m_ci->default_flag() || m_with_collate);
  }
  /*
    Merge to another COLLATE clause. So the full syntax looks like:
      CHARACTER SET cs [COLLATE cl] ... COLLATE cl2
  */
  bool merge_collate_or_error(const Lex_charset_collation_st &cl);
  bool merge_opt_collate_or_error(const Lex_charset_collation_st &cl)
  {
    if (cl.is_empty())
      return false;
    return merge_collate_or_error(cl);
  }
  CHARSET_INFO *charset_and_collation() const { return m_ci; }
  bool with_collate() const { return m_with_collate; }
};


class Lex_charset_collation: public Lex_charset_collation_st
{
public:
  Lex_charset_collation()
  {
    init();
  }
  Lex_charset_collation(CHARSET_INFO *collation, Type type)
  {
    DBUG_ASSERT(collation || type == TYPE_EMPTY);
    m_ci= collation;
    m_type= type;
  }
  explicit Lex_charset_collation(const Lex_explicit_charset_opt_collate &cscl)
   :Lex_charset_collation(cscl.charset_and_collation(),
                          cscl.with_collate() ? TYPE_COLLATE_EXACT :
                                                TYPE_CHARACTER_SET)
  { }
  static Lex_charset_collation national(bool bin_mod)
  {
    return bin_mod ?
      Lex_charset_collation(&my_charset_utf8mb3_bin, TYPE_COLLATE_EXACT) :
      Lex_charset_collation(&my_charset_utf8mb3_general_ci, TYPE_CHARACTER_SET);
  }
};


class Lex_maybe_default_charset_collation:
  public Lex_maybe_default_charset_collation_st
{
public:
  Lex_maybe_default_charset_collation()
  {
    init();
  }
};

#endif // LEX_CHARSET_INCLUDED
