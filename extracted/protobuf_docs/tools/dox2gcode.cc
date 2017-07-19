#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <utility>
#include <algorithm>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <ctype.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/compiler/importer.h>
#include "doxygen.pb.h"
#include "googlecode-doc.pb.h"

using namespace std;
using namespace google::protobuf;

bool IsWhitespace(const string& input) {
  for (string::size_type i = 0; i < input.size(); i++) {
    if (!isspace(input[i])) return false;
  }
  return true;
}

bool StartsWith(const string& input, const char* prefix) {
  size_t len = strlen(prefix);
  return input.size() >= len && strncmp(input.data(), prefix, len) == 0;
}

bool EndsWith(const string& input, const char* suffix) {
  size_t len = strlen(suffix);
  return input.size() >= len &&
         strncmp(input.data() + input.size() - len, suffix, len) == 0;
}

string StringReplace(const string& input, const char* from, const char* to) {
  string::size_type pos = 0;
  string result;
  int from_len = strlen(from);
  while (true) {
    string::size_type newpos = input.find(from, pos);
    result.append(input, pos, newpos - pos);
    if (newpos == string::npos) return result;
    result.append(to);
    pos = newpos + from_len;
  }
}

string RemovePrefix(const string& input, const char* prefix) {
  if (StartsWith(input, prefix)) {
    return input.substr(strlen(prefix));
  } else {
    return input;
  }
}

string RemoveSuffix(const string& input, const char* suffix) {
  int len = strlen(suffix);
  if (strcmp(input.c_str() + input.size() - len, suffix) == 0) {
    return input.substr(0, input.size() - len);
  } else {
    return input;
  }
}

string ExtractAnchor(const string& url) {
  string::size_type pos = url.find_first_of('#');
  GOOGLE_CHECK_NE(pos, string::npos);
  return url.substr(pos + 1);
}

// Perform a lookup in a map or hash_map.
// If the key is present a const pointer to the associated value is returned,
// otherwise a NULL pointer is returned.
template <class Collection>
const typename Collection::value_type::second_type*
FindOrNull(const Collection& collection,
           const typename Collection::value_type::first_type& key) {
  typename Collection::const_iterator it = collection.find(key);
  if (it == collection.end()) {
    return 0;
  }
  return &it->second;
}

// Perform a lookup in a map or hash_map whose values are pointers.
// If the key is present a const pointer to the associated value is returned,
// otherwise a NULL pointer is returned.
// This function does not distinguish between a missing key and a key mapped
// to a NULL value.
template <class Collection>
const typename Collection::value_type::second_type
FindPtrOrNull(const Collection& collection,
              const typename Collection::value_type::first_type& key) {
  typename Collection::const_iterator it = collection.find(key);
  if (it == collection.end()) {
    return 0;
  }
  return it->second;
}

// HACK HACK:  We need to be able to temporarily disable indenting, but
//   io::Printer doesn't provide a way to do that.
class Printer : public io::Printer {
 public:
  Printer(io::ZeroCopyOutputStream* output, char variable_delimiter)
    : io::Printer(output, variable_delimiter),
      indent_level_(0),
      disable_indent_(false) {}

  void Indent() {
    ++indent_level_;
    if (!disable_indent_) io::Printer::Indent();
  }

  void Outdent() {
    --indent_level_;
    if (!disable_indent_) io::Printer::Outdent();
  }

  void DisableIndent() {
    GOOGLE_CHECK(!disable_indent_);
    for (int i = 0; i < indent_level_; i++) io::Printer::Outdent();
    disable_indent_ = true;
  }

  void EnableIndent() {
    GOOGLE_CHECK(disable_indent_);
    for (int i = 0; i < indent_level_; i++) io::Printer::Indent();
    disable_indent_ = false;
  }

 private:
  int indent_level_;
  bool disable_indent_;
};

// ===================================================================

map<string, string> refid_to_url_map_;
vector<pair<const string*, string*> > refids_to_resolve_;
vector<pair<const string*, string*> > implements_to_resolve_;
vector<pair<const doxygen::CmdGroup*, string*> > cmdgroups_to_format_;
vector<pair<const doxygen::TitleCmdGroup*, string*> > titlecmdgroups_to_format_;
vector<pair<const doxygen::Description*, string*> > descriptions_to_format_;
vector<pair<const doxygen::Description*, string*> >
  brief_descriptions_to_format_;
vector<pair<const doxygen::LinkedText*, string*> > linked_text_to_format_;

map<string, const doxygen::Compounddef*> refid_to_compound_map_;

void ResolveLater(const string& refid, string* output) {
  refids_to_resolve_.push_back(make_pair(&refid, output));
}

void ResolveImplementsLater(const string& refid, string* output) {
  implements_to_resolve_.push_back(make_pair(&refid, output));
}

void FormatLater(const doxygen::CmdGroup& dox, string* output) {
  cmdgroups_to_format_.push_back(make_pair(&dox, output));
}

void FormatLater(const doxygen::TitleCmdGroup& dox, string* output) {
  titlecmdgroups_to_format_.push_back(make_pair(&dox, output));
}

void FormatLater(const doxygen::Description& dox, string* output) {
  descriptions_to_format_.push_back(make_pair(&dox, output));
}

void FormatBriefLater(const doxygen::Description& dox, string* output) {
  brief_descriptions_to_format_.push_back(make_pair(&dox, output));
}

void FormatLater(const doxygen::LinkedText& dox, string* output) {
  linked_text_to_format_.push_back(make_pair(&dox, output));
}

const doxygen::Compounddef& FindCompound(const string& refid) {
  const doxygen::Compounddef* result =
    FindPtrOrNull(refid_to_compound_map_, refid);
  GOOGLE_CHECK(result != NULL) << "No such refid: \"" << refid << "\"";
  return *result;
}

bool IsInternal(const string& refid) {
  // TODO(kenton):  Why are refids sometimes empty?  This didn't used to happen.
  if (refid.empty()) return true;
  const doxygen::Compounddef& compound = FindCompound(refid);
  return compound.prot() == doxygen::PROTECTIONKIND_PRIVATE ||
         compound.prot() == doxygen::PROTECTIONKIND_PACKAGE ||
         compound.compoundname().find("::internal::") != string::npos;
}

const doxygen::Compounddef& FindCompound(const doxygen::Ref& ref) {
  return FindCompound(ref.refid());
}

const string* FindUrlOrNull(const string& refid, const string& text) {
  const string* result = FindOrNull(refid_to_url_map_, refid);
  if (result == NULL) {
    cerr << "Unknown ref: " << refid << endl;
    cerr << "  text is: " << text << endl;
  }
  return result;
}

// -------------------------------------------------------------------

string HtmlEscape(const string& text) {
  string result;
  result.reserve(text.size());

  for (string::size_type i = 0; i < text.size(); i++) {
    switch (text[i]) {
      case '<': result.append("&lt;" ); break;
      case '>': result.append("&gt;" ); break;
      case '&': result.append("&amp;"); break;
      case '[': result.append("[[]"); break;  // EZT
      default:
        result.push_back(text[i]);
        break;
    }
  }

  return result;
}

enum UseLinenumbers {
  USE_LINENUMBERS,
  NO_LINENUMBERS
};

void Format(const doxygen::Markup& markup, const string& tag, Printer* output);
void Format(const doxygen::Markup& markup, const string& tag,
            const string& attributes, Printer* output);
void Format(const doxygen::TitleCmdGroup& dox, Printer* output);
void Format(const doxygen::CmdGroup& dox, Printer* output);
void Format(const doxygen::Para& dox, Printer* output);
void Format(const doxygen::List& dox, Printer* output);
void Format(const doxygen::Listing& dox, UseLinenumbers use_linenumbers,
            Printer* output);
void Format(const doxygen::Description& dox, Printer* output);

void FormatMaybeInline(const RepeatedPtrField<doxygen::Para>& dox,
                       Printer* output);

void Format(const doxygen::Markup& markup, const string& tag, Printer* output) {
  output->Print("<$tag$>", "tag", tag);
  for (int i = 0; i < markup.element_size(); i++) {
    Format(markup.element(i), output);
  }
  output->Print("</$tag$>", "tag", tag);
}

void Format(const doxygen::Markup& markup, const string& tag,
            const string& attributes, Printer* output) {
  output->Print("<$tag$ $attributes$>", "tag", tag, "attributes", attributes);
  for (int i = 0; i < markup.element_size(); i++) {
    Format(markup.element(i), output);
  }
  output->Print("</$tag$>", "tag", tag);
}

void Format(const doxygen::TitleCmdGroup& dox, Printer* output) {
  if (dox.has_text()) {
    output->Print("$html$", "html", HtmlEscape(dox.text()));
  } else if (dox.has_ulink()) {
    output->Print("<a href='$url$'>", "url", dox.ulink().url());
    for (int i = 0; i < dox.ref().element_size(); i++) {
      Format(dox.ref().element(i), output);
    }
    output->Print("</a>");
  } else if (dox.has_bold())           { Format(dox.bold          (), "b"     , output);
  } else if (dox.has_emphasis())       { Format(dox.emphasis      (), "i"     , output);
  } else if (dox.has_computeroutput()) { Format(dox.computeroutput(), "code"  , output);
  } else if (dox.has_subscript())      { Format(dox.subscript     (), "sub"   , output);
  } else if (dox.has_superscript())    { Format(dox.superscript   (), "sup"   , output);
  } else if (dox.has_center())         { Format(dox.center        (), "center", output);
//  } else if (dox.has_small())          {
  } else if (dox.has_htmlonly()) {
    output->Print("$html$", "html", dox.htmlonly());
//  } else if (dox.has_latexonly()) {
//  } else if (dox.has_dot()) {
//  } else if (dox.has_anchor()) {
//  } else if (dox.has_formula()) {
  } else if (dox.has_ref()) {
    const string* url = FindUrlOrNull(dox.ref().refid(),
                                      dox.ref().ShortDebugString());
    if (url != NULL) {
      output->Print("<a href='$url$'>", "url", *url);
    }
    for (int i = 0; i < dox.ref().element_size(); i++) {
      Format(dox.ref().element(i), output);
    }
    if (url != NULL) {
      output->Print("</a>");
    }
  } else if (dox.has_copy())       { output->Print("&copy;");
  } else if (dox.has_trademark())  { output->Print("<sup>TM</sup>");
  } else if (dox.has_registered()) { output->Print("&reg;");
  } else if (dox.has_lsquo())      { output->Print("&lsquo;");
  } else if (dox.has_rsquo())      { output->Print("&rsquo;");
  } else if (dox.has_ldquo())      { output->Print("&ldquo;");
  } else if (dox.has_rdquo())      { output->Print("&rdquo;");
  } else if (dox.has_ndash())      { output->Print("&ndash;");
  } else if (dox.has_mdash())      { output->Print("&mdash;");
//  } else if (dox.has_umlaut()) {
//  } else if (dox.has_acute()) {
//  } else if (dox.has_grave()) {
//  } else if (dox.has_circ()) {
//  } else if (dox.has_slash()) {
//  } else if (dox.has_tilde()) {
//  } else if (dox.has_cedil()) {
//  } else if (dox.has_ring()) {
  } else if (dox.has_szlig())             { output->Print("&szlig;");
  } else if (dox.has_nonbreakablespace()) { output->Print("&nbsp;");
  } else {
    cerr << "Don't know how to format TitleCmdGroup: "
         << dox.ShortDebugString();
  }
}

void Format(const doxygen::CmdGroup& dox, Printer* output) {
  if (dox.has_more()) {
    Format(dox.more(), output);
  } else if (dox.has_linebreak()) {
    output->Print("<br>\n");
  } else if (dox.has_hruler()) {
    output->Print("\n<hr>\n");
  } else if (dox.has_preformatted()) {
    output->DisableIndent();
    Format(dox.preformatted(), "pre", output);
    output->EnableIndent();
  } else if (dox.has_programlisting()) {
    Format(dox.programlisting(), NO_LINENUMBERS, output);
  } else if (dox.has_verbatim()) {
    output->DisableIndent();
    output->Print("<pre>$text$</pre>", "text", HtmlEscape(dox.verbatim()));
    output->EnableIndent();
//  } else if (dox.has_indexentry()) {
  } else if (dox.has_orderedlist()) {
    output->Print("<ol>\n");
    output->Indent();
    Format(dox.orderedlist(), output);
    output->Outdent();
    output->Print("</ol>\n");
  } else if (dox.has_itemizedlist()) {
    output->Print("<ul>\n");
    output->Indent();
    Format(dox.itemizedlist(), output);
    output->Outdent();
    output->Print("</ul>\n");
//  } else if (dox.has_simplesect()) {
//  } else if (dox.has_title()) {
//  } else if (dox.has_variablelist()) {
  } else if (dox.has_table()) {
    output->Print("<table>\n");
    output->Indent();
    for (int i = 0; i < dox.table().row_size(); i++) {
      output->Print("<tr>\n");
      output->Indent();
      for (int j = 0; j < dox.table().row(i).entry_size(); j++) {
        const doxygen::Entry& entry = dox.table().row(i).entry(j);
        output->Print(entry.thead() ? "<th>" : "<td>");
        output->Indent();
        FormatMaybeInline(entry.para(), output);
        output->Outdent();
        output->Print(entry.thead() ? "</th>\n" : "</td>\n");
      }
      output->Outdent();
      output->Print("</tr>\n");
    }
    output->Outdent();
    output->Print("</table>\n");
    // TODO(kenton):  Caption?
  } else if (dox.has_heading()) {
    char level = '0' + dox.heading().level();
    output->Print("<h$level$>\n", "level", string(1, level));
    output->Indent();
    Format(dox.orderedlist(), output);
    output->Outdent();
    output->Print("</h$level$>\n", "level", string(1, level));
//  } else if (dox.has_image()) {
//  } else if (dox.has_dotfile()) {
//  } else if (dox.has_toclist()) {
//  } else if (dox.has_language()) {
//  } else if (dox.has_parameterlist()) {
//  } else if (dox.has_xrefsect()) {
//  } else if (dox.has_copydoc()) {
  } else {
    cerr << "Don't know how to format CmdGroup: "
         << dox.ShortDebugString();
  }
}

bool IsInline(const doxygen::CmdGroup& dox) {
  return dox.has_more() || dox.has_linebreak();
}

void Format(const doxygen::Para& dox, Printer* output) {
  // Doxygen allows both inline and block elements to be part of a paragraph.
  // This is wrong.  We need to fix it.
  bool last_was_inline = false;
  for (int i = 0; i < dox.element_size(); i++) {
    if (!last_was_inline && dox.element(i).more().has_text() &&
        IsWhitespace(dox.element(i).more().text())) {
      // skip annoying whitespace after block.
      continue;
    }
    bool is_inline = IsInline(dox.element(i));
    if (!last_was_inline && is_inline) {
      // This is an inline element after a block element.  It should start a
      // new paragraph.
      output->Print("<p>");
    } else if (last_was_inline && !is_inline) {
      // This is a block element after an inline element.  Need to end the
      // paragraph.
      output->Print("</p>\n");
    }
    Format(dox.element(i), output);
    last_was_inline = is_inline;
  }
  if (last_was_inline) {
    output->Print("</p>\n");
  }
}

bool IsAllInline(const doxygen::Para& dox) {
  for (int i = 0; i < dox.element_size(); i++) {
    if (!dox.element(i).has_more() && !dox.element(i).has_linebreak()) {
      // This is a block element.
      return false;
    }
  }
  return true;
}

void FormatMaybeInline(const RepeatedPtrField<doxygen::Para>& dox,
                       Printer* output) {
  if (dox.size() == 1 && IsAllInline(dox.Get(0))) {
    // The contents of this paragraph sequence are all inline tags, so we
    // should not wrap them in a <p>.
    for (int i = 0; i < dox.Get(0).element_size(); i++) {
      Format(dox.Get(0).element(i), output);
    }
  } else {
    output->Print("\n");
    for (int i = 0; i < dox.size(); i++) {
      Format(dox.Get(i), output);
    }
  }
}

void Format(const doxygen::List& dox, Printer* output) {
  for (int i = 0; i < dox.listitem_size(); i++) {
    output->Print("<li>");
    output->Indent();
    FormatMaybeInline(dox.listitem(i).para(), output);
    output->Outdent();
    output->Print("</li>\n");
  }
}

void Format(const doxygen::Listing& dox, UseLinenumbers use_linenumbers,
            Printer* output) {
  output->DisableIndent();
  // We could use class='prettyprint' on this <pre> but frankly I find
  // googlecode's prettyprint highlighting to be ridiculous.  For example,
  // it highlights "GetString" but not "GetInt32".  Non-sensical highlighting
  // only makes things worse.
  output->Print("<pre>");
  for (int i = 0; i < dox.codeline_size(); i++) {
    const doxygen::Codeline& line = dox.codeline(i);

    if (i > 0) output->Print("\n");

    if (use_linenumbers == USE_LINENUMBERS) {
      char lineno[10];
      sprintf(lineno, "%4d", line.lineno());
      output->Print("<span class='linenum'>$num$</span> ", "num", lineno);
    }

    for (int j = 0; j < line.highlight_size(); j++) {
      const doxygen::Highlight& highlight = line.highlight(j);
      for (int k = 0; k < highlight.element_size(); k++) {
        const doxygen::HighlightPiece& piece = highlight.element(k);
        if (piece.has_text()) {
          output->Print("$text$", "text", HtmlEscape(piece.text()));
        } else if (piece.has_sp()) {
          output->Print(" ");
        } else if (piece.has_ref()) {
          const string* url = FindUrlOrNull(piece.ref().refid(),
                                            piece.ref().text());
          if (url == NULL) {
            output->Print("$text$", "text", HtmlEscape(piece.ref().text()));
          } else {
            output->Print(
              "<a href='$url$'>$text$</a>",
              "url", *url,
              "text", HtmlEscape(piece.ref().text()));
          }
        } else {
          cerr << "Don't know how to format HighlightPiece: "
               << piece.ShortDebugString();
        }
      }
    }
  }
  output->EnableIndent();
  output->Print("</pre>\n");
}

void Format(const doxygen::Description& dox, Printer* output) {
  if (!IsWhitespace(dox.text()) || dox.has_title() || dox.sect1_size() > 0) {
    cerr << "doxygen.Description had parts we don't understand:\n"
         << dox.DebugString() << endl;
  }

  for (int i = 0; i < dox.para_size(); i++) {
    Format(dox.para(i), output);
  }
}

void FormatBrief(const doxygen::Description& dox, Printer* output) {
  if (!IsWhitespace(dox.text()) || dox.has_title() || dox.sect1_size() > 0) {
    cerr << "doxygen.Description had parts we don't understand:\n"
         << dox.DebugString() << endl;
  }

  FormatMaybeInline(dox.para(), output);
}

void Format(const doxygen::LinkedText& dox, Printer* output) {
  for (int i = 0; i < dox.element_size(); i++) {
    const doxygen::LinkedTextPiece& piece = dox.element(i);

    if (piece.has_text()) {
/*
      string text = piece.text();
      if (i == dox.element_size() - 1 &&
          EndsWith(text, " *") || EndsWith(text, " &")) {
        // Hack:  Doxygen inserts spaces before * and & modifiers on types.
        //   I dislike this.
        text.erase(text.size() - 2, 1);
      }
*/
      output->Print("$text$", "text", HtmlEscape(piece.text()));
    } else if (piece.has_ref()) {
      const string* url = FindUrlOrNull(piece.ref().refid(),
                                        piece.ref().text());
      if (url == NULL) {
        output->Print("$text$", "text", HtmlEscape(piece.ref().text()));
      } else {
        output->Print(
          "<a href='$url$'>$text$</a>",
          "url", *url,
          "text", HtmlEscape(piece.ref().text()));
      }
    } else {
      cerr << "Don't know how to format LinkedTextPiece: "
           << piece.ShortDebugString();
    }
  }
}

template <typename Type>
void FormatAll(const vector<pair<const Type*, string*> >& items) {
  for (size_t i = 0; i < items.size(); i++) {
    io::StringOutputStream out(items[i].second);
    Printer printer(&out, '$');
    Format(*items[i].first, &printer);
  }
}

template <typename Type>
void FormatAllBrief(const vector<pair<const Type*, string*> >& items) {
  for (size_t i = 0; i < items.size(); i++) {
    io::StringOutputStream out(items[i].second);
    Printer printer(&out, '$');
    FormatBrief(*items[i].first, &printer);
  }
}

bool IsNonEmpty(const doxygen::Description& description) {
  return description.has_title() || description.para_size() > 0;
}

// -------------------------------------------------------------------

const size_t kLongTypeSize = 16;

void SplitNameAndScope(const string& full_name,
                       string* name, string* scope) {
  string::size_type pos = full_name.rfind("::");
  if (pos == string::npos) {
    if (name  != NULL) name ->assign(full_name);
    if (scope != NULL) scope->clear();
  } else {
    if (name  != NULL) name ->assign(full_name, pos + 2, string::npos);
    if (scope != NULL) scope->assign(full_name, 0, pos);
  }
}

void TranslateType(const string& name, const string& refid,
                   googlecode::Type* type) {
  SplitNameAndScope(name, type->mutable_name(), NULL);
  type->set_full_name(name);
  ResolveLater(refid, type->mutable_url());
}

void TranslateExpression(const doxygen::LinkedText& dox,
                         googlecode::Expression* type) {
  string* text = type->mutable_text();

  for (int i = 0; i < dox.element_size(); i++) {
    const doxygen::LinkedTextPiece& piece = dox.element(i);

    if (piece.has_text()) {
      text->append(piece.text());
    } else if (piece.has_ref()) {
      text->append(piece.ref().text());
    }
  }

  FormatLater(dox, type->mutable_linked_text());
}

void TranslateTemplateParameter(const doxygen::Param& dox_param,
                                googlecode::TemplateParameter* param) {
  param->set_name(dox_param.declname());
  if (dox_param.has_defval()) {
    TranslateExpression(dox_param.defval(), param->mutable_default_value());
  }
}

void TranslateVariable(const doxygen::Memberdef& dox_member,
                       googlecode::Variable* variable) {
  if (dox_member.static_ ()) { variable->add_prefix("static" ); }
  if (dox_member.const_  ()) { variable->add_prefix("const"  ); }
  if (dox_member.mutable_()) { variable->add_prefix("mutable"); }

  TranslateExpression(dox_member.type(), variable->mutable_type());
  if (variable->type().text().size() > kLongTypeSize) {
    variable->set_type_is_long(true);
  }
  if (dox_member.has_initializer()) {
    TranslateExpression(dox_member.initializer(), variable->mutable_value());
  }
}

void TranslateTypedef(const doxygen::Memberdef& dox_member,
                      googlecode::Typedef* typedef_type) {
  TranslateExpression(dox_member.type(), typedef_type->mutable_type());
  if (!IsWhitespace(dox_member.argsstring())) {
    typedef_type->set_function_args(dox_member.argsstring());
  }
}

void TranslateEnum(const doxygen::Memberdef& dox_member,
                   const string& url,
                   googlecode::Enum* enum_type) {
  for (int i = 0; i < dox_member.enumvalue_size(); i++) {
    const doxygen::Enumvalue& dox_value = dox_member.enumvalue(i);
    googlecode::Enum::Value* value = enum_type->add_value();

    refid_to_url_map_[dox_value.id()] = url;

    value->set_name(dox_value.name());
    if (dox_value.has_initializer()) {
      TranslateExpression(dox_value.initializer(), value->mutable_value());
    }
    if (IsNonEmpty(dox_value.briefdescription())) {
      FormatBriefLater(dox_value.briefdescription(), value->mutable_brief());
    }
    if (IsNonEmpty(dox_value.detaileddescription())) {
      FormatLater(dox_value.detaileddescription(), value->mutable_details());
    }
  }
}

void TranslateFunction(const doxygen::Memberdef& dox_member,
                       googlecode::Function* function) {
  if (dox_member.virt() != doxygen::VIRTUALKIND_NON_VIRTUAL) {
    function->add_prefix("virtual");
  }

  if (dox_member.static_  ()) { function->add_prefix("static"  ); }
  if (dox_member.explicit_()) { function->add_prefix("explicit"); }
//  if (dox_member.inline_  ()) { function->add_prefix("inline"  ); }

  if (dox_member.const_()) { function->add_suffix("const"   ); }
  if (dox_member.virt() == doxygen::VIRTUALKIND_PURE_VIRTUAL) {
    function->add_suffix(" = 0");
  }

  // Virtual destructors should have no type, but Doxygen reports that their
  // type is simply "virtual".  Note that it still sets dox_member.virt()
  // correctly and it never adds "virtual" to the type string for a method
  // that actually has a type.
  if (!StartsWith(dox_member.name(), "~")) {
    TranslateExpression(dox_member.type(), function->mutable_type());
    if (function->type().text().size() > kLongTypeSize) {
      function->set_type_is_long(true);
    }
  }

  for (int i = 0; i < dox_member.param_size(); i++) {
    const doxygen::Param& dox_param = dox_member.param(i);
    googlecode::Function::Parameter* param = function->add_parameter();

    TranslateExpression(dox_param.type(), param->mutable_type());
    param->set_name(dox_param.declname());
    if (dox_param.has_defval()) {
      TranslateExpression(dox_param.defval(), param->mutable_default_value());
    }
    if (IsNonEmpty(dox_param.briefdescription())) {
      FormatLater(dox_param.briefdescription(), param->mutable_description());
    }
  }

  for (int i = 0; i < dox_member.templateparamlist().param_size(); i++) {
    TranslateTemplateParameter(dox_member.templateparamlist().param(i),
                               function->add_template_parameter());
  }
}

void TranslateMember(const doxygen::Memberdef& dox_member,
                     const doxygen::Compounddef& scope,
                     const string& url_prefix,
                     googlecode::Member* member) {
  member->set_name(dox_member.name());
  TranslateType(scope.compoundname(), scope.id(), member->mutable_scope());
  member->set_url(url_prefix + dox_member.name());
  member->set_anchor(ExtractAnchor(member->url()));
  refid_to_url_map_[dox_member.id()] = member->url();
  if (IsNonEmpty(dox_member.briefdescription())) {
    FormatBriefLater(dox_member.briefdescription(), member->mutable_brief());
  }
  if (IsNonEmpty(dox_member.detaileddescription())) {
    FormatLater(dox_member.detaileddescription(), member->mutable_details());
  }
  if (dox_member.prot() == doxygen::PROTECTIONKIND_PROTECTED) {
    member->set_is_protected(true);
  }

  switch (dox_member.kind()) {
    case doxygen::MEMBERKIND_VARIABLE:
      TranslateVariable(dox_member, member->mutable_variable());
      break;
    case doxygen::MEMBERKIND_TYPEDEF:
      TranslateTypedef(dox_member, member->mutable_typedef_type());
      break;
    case doxygen::MEMBERKIND_ENUM:
      TranslateEnum(dox_member, member->url(), member->mutable_enum_type());
      // Force enums to have descriptions so that they will always generate
      // a detailed section.
      member->mutable_brief();
      member->mutable_details();
      break;
    case doxygen::MEMBERKIND_FUNCTION:
      TranslateFunction(dox_member, member->mutable_function());
      break;
    default:
      break;
  }
}

void MaybeTranslateMember(const doxygen::Memberdef& dox_member,
                          const doxygen::Compounddef& scope,
                          const string& url_prefix,
                          RepeatedPtrField<googlecode::Member>* members) {
  if (dox_member.name().find("__pad") != string::npos) {
    // Doxygen looks at the back insert iterator functions at the end
    // of repeated_field.h and apparently decides that they are bitfields.
    // How template functions look anything like bitfields I do not know.
    // Furthermore, it thinks they are unnamed bitfields, so it assigns
    // them dummy names of the form "__padN__", where N is a digit.  Since
    // it totally fails on these we'll just have to hide them.
    return;
  }

  switch (dox_member.prot()) {
    case doxygen::PROTECTIONKIND_PUBLIC:
    case doxygen::PROTECTIONKIND_PROTECTED:
      switch (dox_member.kind()) {
        case doxygen::MEMBERKIND_VARIABLE:
        case doxygen::MEMBERKIND_TYPEDEF:
        case doxygen::MEMBERKIND_ENUM:
        case doxygen::MEMBERKIND_FUNCTION:
          TranslateMember(dox_member, scope, url_prefix,
                          members->Add());
          break;
        default:
          // We don't care about documenting other crap.
          break;
      }
      break;
    default:
      break;
  }
}

void TranslateMembers(const doxygen::Sectiondef& dox_section,
                      const doxygen::Compounddef& scope,
                      const string& url_prefix,
                      RepeatedPtrField<googlecode::Member>* members) {
  for (int i = 0; i < dox_section.memberdef_size(); i++) {
    MaybeTranslateMember(dox_section.memberdef(i), scope, url_prefix, members);
  }
}

const doxygen::Compounddef* FindSuperclass(
    const doxygen::Compounddef& dox_class,
    const string& superclass_name) {
  if (dox_class.basecompoundref_size() == 0) {
    return NULL;
  }

  const doxygen::Compounddef& superclass =
    FindCompound(dox_class.basecompoundref(0).refid());
  if (superclass.compoundname() == superclass_name ||
      EndsWith(superclass.compoundname(), ("::" + superclass_name).c_str())) {
    return &superclass;
  } else {
    return FindSuperclass(superclass, superclass_name);
  }
}

void TranslateSection(const doxygen::Sectiondef& dox_section,
                      const doxygen::Compounddef& dox_class,
                      const string& url_prefix,
                      googlecode::Section* section) {
  if (dox_section.has_header()) {
    if (StartsWith(dox_section.header(), "implements ")) {
      string target = dox_section.header().substr(strlen("implements "));
      // Search for the target class.
      const doxygen::Compounddef* super = FindSuperclass(dox_class, target);
      if (super == NULL) {
        cerr << "Failed to find superclass referenced by section title: "
             << dox_section.header() << endl;
        section->set_title(dox_section.header());
      } else {
        // Put the target name as we saw it into the title for now.  We'll
        // fix it up later.
        section->set_title(target);
        ResolveImplementsLater(super->id(), section->mutable_title());
      }
    } else {
      section->set_title(dox_section.header());
    }
  }
  if (dox_section.has_description()) {
    FormatBriefLater(dox_section.description(), section->mutable_description());
  }
  TranslateMembers(dox_section, dox_class, url_prefix,
                   section->mutable_member());
}

void TranslateClass(const doxygen::Compounddef& dox_class,
                    const doxygen::Compounddef& dox_file,
                    const doxygen::Compounddef& dox_namespace,
                    const string& url_prefix,
                    googlecode::File* file,
                    googlecode::Class* class_type) {
  class_type->set_name(
    dox_class.compoundname().substr(
      dox_namespace.compoundname().size() + 2));
  TranslateType(dox_namespace.compoundname(),
                dox_namespace.id(), class_type->mutable_scope());
  string short_name;
  SplitNameAndScope(dox_class.compoundname(), &short_name, NULL);
  class_type->set_url(url_prefix + short_name);
  class_type->set_anchor(ExtractAnchor(class_type->url()));
  refid_to_url_map_[dox_class.id()] = class_type->url();

  if (dox_file.compoundname() == "descriptor.pb.h") {
    class_type->set_brief(
      "See the docs for <a href='#'>descriptor.pb.h</a> for more "
      "information about this class.");
  } else if (dox_file.compoundname() == "plugin.pb.h") {
    class_type->set_brief(
      "See the docs for <a href='#'>plugin.pb.h</a> for more "
      "information about this class.");
  } else {
    if (IsNonEmpty(dox_class.briefdescription())) {
      FormatBriefLater(dox_class.briefdescription(),
                       class_type->mutable_brief());
    }
    if (IsNonEmpty(dox_class.detaileddescription())) {
      FormatLater(dox_class.detaileddescription(),
                  class_type->mutable_details());
    }
  }

  if (dox_class.kind() == doxygen::COMPOUNDKIND_STRUCT) {
    class_type->set_kind("struct");
  } else if (dox_class.kind() == doxygen::COMPOUNDKIND_UNION) {
    class_type->set_kind("union");
  } else if (dox_class.kind() != doxygen::COMPOUNDKIND_CLASS) {
    cerr << dox_class.compoundname() << " is not a class, struct, or union."
         << endl;
  }

  if (dox_class.basecompoundref_size() > 0) {
    if (!IsInternal(dox_class.basecompoundref(0).refid())) {
      TranslateType(dox_class.basecompoundref(0).text(),
                    dox_class.basecompoundref(0).refid(),
                    class_type->mutable_superclass());
    }

    if (dox_class.basecompoundref_size() > 1) {
      cerr << dox_class.compoundname()
           << " has multiple superclasses." << endl;
    }
  }

  for (int i = 0; i < dox_class.derivedcompoundref_size(); i++) {
    if (!IsInternal(dox_class.derivedcompoundref(i).refid())) {
      TranslateType(dox_class.derivedcompoundref(i).text(),
                    dox_class.derivedcompoundref(i).refid(),
                    class_type->add_subclass());
    }
  }

  for (int i = 0; i < dox_class.templateparamlist().param_size(); i++) {
    TranslateTemplateParameter(dox_class.templateparamlist().param(i),
                               class_type->add_template_parameter());
  }

  string sub_prefix = class_type->url() + ".";
  for (int i = 0; i < dox_class.innerclass_size(); i++) {
    const doxygen::Compounddef& inner = FindCompound(dox_class.innerclass(i));
    if (inner.compoundname().find("::TypeHandler") != string::npos) {
      // RepeatedPtrField::TypeHandler is private but Doxygen thinks it is
      // public.  Shoot it.
      continue;
    }
    if (inner.prot() == doxygen::PROTECTIONKIND_PUBLIC) {
      TranslateType(dox_class.innerclass(i).text(),
                    dox_class.innerclass(i).refid(),
                    class_type->add_inner_class());
      TranslateClass(inner, dox_file, dox_namespace, sub_prefix,
                     file, file->add_class_type());
    }
  }

  for (int i = 0; i < dox_class.sectiondef_size(); i++) {
    const doxygen::Sectiondef& dox_section = dox_class.sectiondef(i);
    if (dox_section.kind() == doxygen::SECTIONKIND_USER_DEFINED) {
      googlecode::Section* section = class_type->add_section();
      TranslateSection(dox_section, dox_class, sub_prefix, section);
      if (section->member_size() == 0) {
        class_type->mutable_section()->RemoveLast();
      }
    } else {
      TranslateMembers(dox_section, dox_class, sub_prefix,
                       class_type->mutable_member());
    }
  }
}

void ReadFile(const string& filename, string* contents) {
  fstream in(filename.c_str(), ios::in);
  if (!in) {
    cerr << "Not found: " << filename << endl;
    exit(1);
  }
  string line;
  while (getline(in, line)) {
    contents->append(line);
    contents->push_back('\n');
  }
}

void TranslateFile(const doxygen::Compounddef& dox_file,
                   const doxygen::Compounddef& dox_namespace,
                   const string& package_name,
                   const string& package_url,
                   const string& path,
                   googlecode::File* file) {
  file->set_name(dox_file.compoundname());
  file->set_breakable_name(
    StringReplace(dox_file.compoundname(), "_", "_<wbr>"));
  file->set_url(package_name + "." +
                RemoveSuffix(file->name(), ".h"));
  refid_to_url_map_[dox_file.id()] = file->url();

  if (file->name() == "descriptor.pb.h") {
    // descriptor.pb.h has no auto-generated docs.
    file->set_brief("Protocol buffer representations of descriptors.");
    file->set_details(
      "<p>This file defines a set of protocol message classes which represent "
      "the same information represented by the classes defined in "
      "<a href='google.protobuf.descriptor.html'>descriptor.h</a>.  "
      "You can convert a "
      "<a href='#FileDescriptorProto'>FileDescriptorProto</a> to a "
      "<a href='google.protobuf.descriptor.html#FileDescriptor'>"
      "FileDescriptor</a> using the "
      "<a href='google.protobuf.descriptor.html#DescriptorPool'>"
      "DescriptorPool</a> class.  Thus, the classes in this file allow "
      "protocol type definitions to be communicated efficiently between "
      "processes.</p>\n"
      "<p>The protocol compiler currently doesn't support auto-generated "
      "documentation, hence this page contains no descriptions.  This file "
      "was generated by the protocol compiler from "
      "<code>descriptor.proto</code>, whose contents are as follows:</p>\n"
      "<pre>");
    string descriptor_proto_contents;
    ReadFile("protobuf/src/google/protobuf/descriptor.proto",
             &descriptor_proto_contents);
    file->mutable_details()->append(HtmlEscape(descriptor_proto_contents));
    file->mutable_details()->append("</pre>");
  } else if (file->name() == "plugin.pb.h") {
    // plugin.pb.h has no auto-generated docs.
    file->set_brief("API for protoc plugins.");
    file->set_details(
      "<p>This file defines a set of protocol message classes which make up "
      "the API to protoc code generator plugins.  Plugins written in C++ "
      "should probably build on the API in "
      "<a href='google.protobuf.compiler.plugin.html'>plugin.h</a> instead "
      "of dealing with the protobuf-level API, but plugins in other languages "
      "will need to deal with the raw messages as defined below.</p>"
      "<p>The protocol compiler currently doesn't support auto-generated "
      "documentation, hence this page contains no descriptions.  This file "
      "was generated by the protocol compiler from "
      "<code>plugin.proto</code>, whose contents are as follows:</p>\n"
      "<pre>");
    string descriptor_proto_contents;
    ReadFile("protobuf/src/google/protobuf/compiler/plugin.proto",
             &descriptor_proto_contents);
    file->mutable_details()->append(HtmlEscape(descriptor_proto_contents));
    file->mutable_details()->append("</pre>");
  } else {
    // Docs will be found on a class whose name ends with _DoxygenScopedDocHack.
    // See comment in doxygen_filter.py.
  }

  file->set_namespace_name(dox_namespace.compoundname());
  file->set_package_url(package_url);
  if (EndsWith(path, "/")) {
    file->set_full_path(path + file->name());
  } else {
    file->set_full_path(path + "/" + file->name());
  }

  string sub_prefix = file->url() + '#';

  for (int i = 0; i < dox_file.innerclass_size(); i++) {
    const doxygen::Compounddef& dox_class =
      FindCompound(dox_file.innerclass(i));
    if (EndsWith(dox_class.compoundname(), "_DoxygenScopedDocHack")) {
      // This class has our file-level docs.  See comment in doxygen_filter.py.
      if (!file->has_brief()) {
        if (IsNonEmpty(dox_class.briefdescription())) {
          FormatBriefLater(dox_class.briefdescription(), file->mutable_brief());
        }
        if (IsNonEmpty(dox_class.detaileddescription())) {
          FormatLater(dox_class.detaileddescription(), file->mutable_details());
        }
      }
    } else {
      string class_scope;
      SplitNameAndScope(dox_class.compoundname(), NULL, &class_scope);
      if (class_scope == dox_namespace.compoundname()) {
        TranslateClass(dox_class, dox_file, dox_namespace, sub_prefix,
                       file, file->add_class_type());
      }
    }
  }

  for (int i = 0; i < dox_file.sectiondef_size(); i++) {
    const doxygen::Sectiondef& dox_section = dox_file.sectiondef(i);
    TranslateMembers(dox_section, dox_namespace, sub_prefix,
                     file->mutable_member());
  }

  // Doxygen makes some things be members of the *namespace* rather than the
  // file and gives us no way to figure out which is which.
  for (int i = 0; i < dox_namespace.sectiondef_size(); i++) {
    const doxygen::Sectiondef& dox_section = dox_namespace.sectiondef(i);

    for (int i = 0; i < dox_section.memberdef_size(); i++) {
      const doxygen::Memberdef& dox_member = dox_section.memberdef(i);
      if (EndsWith(dox_member.location().file(), file->full_path().c_str())) {
        MaybeTranslateMember(dox_member, dox_namespace, sub_prefix,
                             file->mutable_member());
      }
    }
  }
}

void TranslatePackage(const doxygen::Compounddef& dox_dir,
                      const doxygen::Compounddef& dox_namespace,
                      googlecode::Package* package) {
  string dot_name =
    StringReplace(RemoveSuffix(dox_dir.compoundname(), "/"), "/", ".");
  package->set_name(StringReplace(dot_name, ".", "::"));
  package->set_short_name(RemovePrefix(package->name(), "google::"));
  if (IsNonEmpty(dox_namespace.briefdescription())) {
    FormatBriefLater(dox_namespace.briefdescription(),
                     package->mutable_brief());
  }
  if (IsNonEmpty(dox_namespace.detaileddescription())) {
    FormatLater(dox_namespace.detaileddescription(),
                package->mutable_details());
  }

  package->set_url("index#" + dot_name);
  package->set_anchor(dot_name);
  refid_to_url_map_[dox_dir.id()] = package->url();
  refid_to_url_map_[dox_namespace.id()] = package->url();

  for (int i = 0; i < dox_dir.innerfile_size(); i++) {
    const doxygen::Compounddef& dox_file = FindCompound(dox_dir.innerfile(i));
    if (dox_file.compoundname() != "package_info.h") {
      TranslateFile(dox_file, dox_namespace,
                    dot_name, package->url(), dox_dir.compoundname(),
                    package->add_file());
    }
  }
}

struct PackageOrdering {
  inline bool operator()(const googlecode::Package& a,
                         const googlecode::Package& b) {
    // This coincidentally produces the ordering I want for protobuf.
    return a.name().size() < b.name().size();
  }
};

void Translate(const doxygen::Doxygen& dox, googlecode::Site* site) {
  map<string, const doxygen::Compounddef*> namespace_map;

  // Populate refid_to_compound_map_ and namespace_map...
  for (int i = 0; i < dox.compounddef_size(); i++) {
    const doxygen::Compounddef& compound = dox.compounddef(i);

    refid_to_compound_map_[compound.id()] = &compound;

    if (compound.kind() == doxygen::COMPOUNDKIND_NAMESPACE) {
      namespace_map[compound.compoundname()] = &compound;
    }
  }

  // Find all dirs.
  const doxygen::Compounddef* stubs_dir = NULL;
  googlecode::Package* protobuf_pkg = NULL;
  googlecode::Package* compiler_pkg = NULL;
  map<string, const doxygen::Compounddef*> compiler_dir_map;
  const static char* kCompilerDir = "google/protobuf/compiler/";
  for (int i = 0; i < dox.compounddef_size(); i++) {
    const doxygen::Compounddef& compound = dox.compounddef(i);

    if (compound.kind() == doxygen::COMPOUNDKIND_DIR &&
        StartsWith(compound.compoundname(), "google/protobuf")) {
      if (StartsWith(compound.compoundname(), "google/protobuf/stubs")) {
        stubs_dir = &compound;
      } else if (StartsWith(compound.compoundname(), kCompilerDir)) {
        string lang = RemoveSuffix(
            RemovePrefix(compound.compoundname(), kCompilerDir), "/");
        compiler_dir_map.insert(make_pair(lang, &compound));
        cerr << "lang = " << lang << endl;
      } else {
        string namespace_name =
          StringReplace(RemoveSuffix(compound.compoundname(), "/"), "/", "::");

        const doxygen::Compounddef* dox_namespace =
          FindPtrOrNull(namespace_map, namespace_name);

        GOOGLE_CHECK(dox_namespace != NULL) << "Namespace " << namespace_name
                                            << " in " << compound.compoundname()
                                            << " not found.";
        googlecode::Package* package = site->add_package();
        TranslatePackage(compound, *dox_namespace, package);

        if (namespace_name == "google::protobuf") {
          protobuf_pkg = package;
        } else if (namespace_name == "google::protobuf::compiler") {
          compiler_pkg = package;
        }
      }
    }
  }
  GOOGLE_CHECK(protobuf_pkg != NULL) << "'google/protobuf' package not found.";
  GOOGLE_CHECK(compiler_pkg != NULL)
      << "'google/protobuf/compiler' package not found";

  // Add stubs dir.
  GOOGLE_CHECK_EQ(stubs_dir->innerfile_size(), 1);
  TranslateFile(FindCompound(stubs_dir->innerfile(0)),
                *FindPtrOrNull(namespace_map, "google::protobuf"),
                "google.protobuf", protobuf_pkg->url(),
                stubs_dir->compoundname(), protobuf_pkg->add_file());

  // Add generator dirs.
  for (map<string, const doxygen::Compounddef*>::const_iterator iter =
           compiler_dir_map.begin();
       iter != compiler_dir_map.end(); ++iter) {
    const doxygen::Compounddef* compound_namespace = FindPtrOrNull(
        namespace_map, "google::protobuf::compiler::" + iter->first);
    GOOGLE_CHECK(compound_namespace != NULL);
    for (int i = 0; i < iter->second->innerfile_size(); ++i) {
      TranslateFile(FindCompound(iter->second->innerfile(i)),
                    *compound_namespace,
                    "google.protobuf.compiler", compiler_pkg->url(),
                    iter->second->compoundname(), compiler_pkg->add_file());
    }
    refid_to_url_map_[compound_namespace->id()] = compiler_pkg->url();
  }

  // Resolve refs.
  for (size_t i = 0; i < refids_to_resolve_.size(); i++) {
    const string& refid = *refids_to_resolve_[i].first;
    string* target = refids_to_resolve_[i].second;

    const string* url = FindUrlOrNull(refid, "");
    if (url != NULL) *target = *url;
  }
  for (size_t i = 0; i < implements_to_resolve_.size(); i++) {
    const string& refid = *implements_to_resolve_[i].first;
    string* target = implements_to_resolve_[i].second;

    const string* url = FindUrlOrNull(refid, "");

    string name = *target;

    target->assign("implements ");
    if (url != NULL) {
      target->append("<a href='");
      target->append(*url);
      target->append("'>");
    }
    target->append(name);
    if (url != NULL) {
      target->append("</a>");
    }
  }

  // Format stuff.
  FormatAll(cmdgroups_to_format_);
  FormatAll(titlecmdgroups_to_format_);
  FormatAll(descriptions_to_format_);
  FormatAllBrief(brief_descriptions_to_format_);
  FormatAll(linked_text_to_format_);

  // Sort.
  sort(site->mutable_package()->begin(),
       site->mutable_package()->end(),
       PackageOrdering());
}

// ===================================================================

enum ProtobufEncoding {
  ENCODING_TEXT,
  ENCODING_BINARY
};

bool Run(ProtobufEncoding input_encoding, ProtobufEncoding output_encoding) {
  doxygen::Doxygen dox;
  googlecode::Site site;

  {
    io::FileInputStream stdin(0);
    bool parsed = false;
    switch (input_encoding) {
      case ENCODING_TEXT:
        parsed = TextFormat::Parse(&stdin, &dox);
        break;
      case ENCODING_BINARY:
        parsed = dox.ParseFromZeroCopyStream(&stdin);
        break;
    }

    if (!parsed) {
      cerr << "Failed to parse input." << endl;
      return false;
    }
  }

  Translate(dox, &site);

  if (!site.IsInitialized()) {
    vector<string> errors;
    site.FindInitializationErrors(&errors);
    cerr << "Missing required fields in output:" << endl;
    for (size_t i = 0; i < errors.size(); i++) {
      cerr << "  " << errors[i] << endl;
    }
  }

  {
    io::FileOutputStream stdout(1);
    switch (input_encoding) {
      case ENCODING_TEXT:
        TextFormat::Print(site, &stdout);
        break;
      case ENCODING_BINARY:
        dox.SerializePartialToZeroCopyStream(&stdout);
        break;
    }
  }

  return true;
}

// ===================================================================

const char* program_name_ = NULL;

void Usage() {
  cerr << "Usage:" << endl;
  cerr << "  " << program_name_
       << " [--input=(text|binary)] [--output=(text|binary)]"
       << endl;
  exit(1);
}

bool StartsWith(const char* str, const char* prefix) {
  return strncmp(str, prefix, strlen(prefix)) == 0;
}

bool ParseArg(const char* arg, const char* name, const char** value) {
  if (StartsWith(arg, name)) {
    if (value == NULL) {
      cerr << "Value not expected for flag: " << name << endl;
      Usage();
    }
    *value = arg + strlen(name);
    return true;
  }
  return false;
}

ProtobufEncoding ParseProtobufEncoding(const string& name) {
  if (name == "text") {
    return ENCODING_TEXT;
  } else if (name == "binary") {
    return ENCODING_BINARY;
  } else {
    cerr << "Unknown protobuf encoding: " << name << endl;
    Usage();
    return ENCODING_TEXT;  // Will never get here.
  }
}

int main(int argc, char* argv[]) {
  program_name_ = argv[0];
  const char* encoding_name = NULL;
  ProtobufEncoding input_encoding = ENCODING_TEXT;
  ProtobufEncoding output_encoding = ENCODING_TEXT;

  --argc;
  ++argv;
  while (argc > 0) {
    if (ParseArg(argv[0], "--input=", &encoding_name)) {
      input_encoding = ParseProtobufEncoding(encoding_name);
    } else if (ParseArg(argv[0], "--output=", &encoding_name)) {
      output_encoding = ParseProtobufEncoding(encoding_name);
    } else {
      cerr << "Unknown argument: " << argv[0] << endl;
      Usage();
    }
    --argc;
    ++argv;
  }

  if (Run(input_encoding, output_encoding)) {
    return 0;
  } else {
    return 1;
  }
}
