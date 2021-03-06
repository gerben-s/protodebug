#! /usr/bin/python
#
# Doxygen does not recognize comments that are not in a specific format.
# Unfortunately, our C++ style guide explicitly forbids us from writing
# doc comments in said format.  Then again, I didn't really want to make
# all my comments be doxygen-formatted anyway, since it's kind of ugly
# for people who are reading the raw headers, as most of us do.  However,
# the open source release should probably have actual web-based API
# reference docs.
#
# This here steaming pile of Python works as a Doxygen input filter which
# automatically translates the google/protobuf header comments to the format
# Doxygen wants, complete

import re
import sys

# Class which reads from an input file one line at a time and allows you to
# match that line against regexes.
class Reader:
  def __init__(self, file):
    self.file = file
    self.line = file.readline()

  def Eof(self):
    return self.line == ""

  def LookingAt(self, pattern):
    return re.match(pattern, self.line) != None

  def ConsumeAndWrite(self, out):
    out.write(self.line)
    self.line = self.file.readline()

  def Consume(self):
    line = self.line
    self.line = self.file.readline()
    return line

  def GetIndent(self):
    return re.match(" *", self.line).end()

# If we insert additional lines into the output, we will want to remove lines
# somewhere else so that definitions generally appear on the line number they
# were on originally.  Otherwise, when Doxygen links into the orignial headers,
# it links to the wrong line numbers.  "offset" counts the number of lines we
# need to make up.
class OffsetTrackingFile:
  def __init__(self, file):
    self.file = file
    self.offset = 0
  def write(self, text):
    self.file.write(text)
  def writelines(self, lines):
    self.file.writelines(lines)

# This class keeps tracks of what member groups we've opened, so that we can
# write their end tags later on.  Doxygen does not allow nested groups, though
# nested classes can have their own groups.  Thus, we can keep track of groups
# according to their indentation levels.
class GroupTracker:
  def __init__(self):
    self.levels = []

  # Start a new group indented by the given number of spaces.
  def StartGroup(self, indent, out):
    self.EndGroupsDeeperThan(indent - 1, out)
    out.write(" " * indent + "/// @{\n")
    out.offset += 1
    self.levels.append(indent)

  # Write end tags for all open groups who are indented more than the given
  # number of spaces.
  def EndGroupsDeeperThan(self, indent, out):
    while self.levels and self.levels[-1] > indent:
      out.write(" " * self.levels[-1] + "/// @}\n")
      out.offset += 1
      self.levels.pop()

# Converts a regular comment block into a Doxygen-format comment block and
# writes it to the output.
#
# Other than converting all the "//"s to "///"s, we also try to detect code
# fragments embedded inside comments.  Throughout the comments in protobuf,
# such fragments are always indented two spaces, e.g. like this:
#   // Here is a code fragment:
#   //   int foo = Foo();
#   //   // A comment within the code fragment.
#   //   Bar(foo);
#   // That was a code fragment!
# We try to detect this and put the fragment in a @code block so that Doxygen
# will format it nicely.  However, not everything which is indented is a code
# block.  Another common case is lists, which look like:
#   // Here is a list:
#   // * List item 1.
#   //   This is still part of list item 1.
#   //   Notice the indent.
#   // * List item 2.
#   // The list is over now.
# So, we want to detect lists and *not* interpret them as code.
def FormatDocComment(comment, out):
  min_indent_for_code = 3
  in_code = False
  empty_lines = []
  for line in comment:
    # Doxygen gets confused when it sees "*/" inside a // comment and silently
    # fails to parse anything in the file.  Thanks, Doxygen.
    line = line.replace("*/", "* /")

    # Separate the comment text from the pre-comment indent.
    (outer_indent, content) = line.split("//", 1)
    if content.strip() == "":
      # The line was empty.  Buffer it for now.
      empty_lines.append(outer_indent + "///" + content)
      continue

    # How many spaces is this line indented *after* the comment start?  E.g.
    # the line "  // foo" has a two-space outer indent and a one-space
    # inner_indent.
    inner_indent = re.match(" *", content).end()

    if inner_indent >= min_indent_for_code:
      # Content is indented enough to be a code fragment.
      if not in_code:
        # Begin a code block.
        # Flush empty lines first.
        out.writelines(empty_lines)
        empty_lines = []
        out.write("%s///%s@code\n" %
                  (outer_indent, " " * (min_indent_for_code - 2)))
        out.offset += 1
        in_code = True

      # Doxygen takes all the raw bytes between @code and @encode and copies
      # them directly into the final web page.  It does not look for newlines
      # in this range at all.  So, if you have something reasonable like:
      #   /// @code
      #   /// int foo = 1;
      #   /// @endcode
      # Your code fragment ends up being:
      #   /// int foo = 1;
      #   ///
      # That is, it includes the stupid "///"s, which obviously aren't supposed
      # to be part of the fragment.  Instead, you have to do:
      #   /// @code
      #   int foo = 1;
      #   @endcode
      # Which is, of course, totally invalid C++.  But luckily we don't need
      # this to compile as C++.  We just need Doxygen to read it.
      #
      # Before you ask, yes, I tried doing this:
      #   /** @code
      #   int foo = 1;
      #   @endcode */
      # For whatever reason, Doxygen didn't like it.
      out.write("\n" * len(empty_lines))
      empty_lines = []
      out.write(content[min_indent_for_code:])
    else:
      if in_code:
        # End a code block.  Do not flush empty lines; we don't want to
        # include them in the block.
        out.write("@endcode\n")
        out.offset += 1
        in_code = False

      # Replace '*' bullet style with '-', since Doxygen only recognizes the
      # latter.
      content = re.sub("^( +)[*] ", "\\1- ", content)

      if re.match(" +- ", content):
        # This line starts a bullet list.  Subsequent lines will be indented,
        # but should not be code.
        min_indent_for_code = inner_indent + 4
      elif re.match(".*\\w.*:.*\\w.*$", content):
        # This line starts a note.  Subsequent lines will be indented, but
        # should not be code.
        #
        # Note:  "Note" comments look like this.  They start with a word
        #   followed by a colon (e.g. "Note:", "TODO(kenton):", etc.).  A
        #   line which merely ends with a colon, however, should *not* be
        #   considered a note, because indented code blocks are often
        #   preceeded by such lines.
        min_indent_for_code = inner_indent + 4
      else:
        # Nothing special here.  If the next line is indented then we will
        # put it in a code block.
        min_indent_for_code = inner_indent + 2

      out.writelines(empty_lines)
      empty_lines = []
      out.write(outer_indent + "///" + content)

  if in_code:
    # End a code block.  Do not flush empty lines; we don't want to
    # include them in the block.
    out.write("@endcode\n")
    out.offset += 1
  # Now flush empty lines.
  out.writelines(empty_lines)

# Reads a comment block.  Returns a list of lines.
def ReadComment(reader):
  assert reader.LookingAt(" *//")
  comment = []
  while reader.LookingAt(" *//"):
    comment.append(reader.Consume())
  return comment

# Reads a comment block and writes it to the output.  If the comment appears to
# be documenting something, translates it to Doxygen format in the process.
def HandleComment(reader, out, group_tracker):
  if reader.Eof(): return

  comment = ReadComment(reader)

  # Figure out what to do with it.
  if ((comment[0].count("----") > 0 or comment[0].count("====") > 0) and
      comment[0].startswith("  ")):
    # The comment is a divider.  Put everything under it into a group.
    group_tracker.StartGroup(comment[0].index("//"), out)

    # Remove the -'s and ='s.
    comment[0] = comment[0].rstrip(" -=\r\n") + "\n";
    if comment[0] == "//":
      # The first line is entirely composed of -'s or ='s.  Remove it.
      comment = comment[1:]
      # We know the offset is at least 1 from the StartGroup() call, so we
      # should be able to decrement it.
      assert offset > 0
      out.offset -= 1
    else:
      comment[0] = comment[0].replace("//", "// @name")

    # If the comment is non-empty, we want to write it even if it is not
    # followed by any content, since it documents the group.
    if comment:
      FormatDocComment(comment, out)
    # We need to ensure there is a blank line here so that the group's
    # doc comment doesn't run up against the doc comment for the first thing
    # in it.
    out.write("\n")
    out.offset += 1
  elif (# A line with alphanumeric characters is considered to be content.
        reader.LookingAt(".*\\w.*") and
        # Unless all the characters are in a comment.
        not reader.LookingAt("\\W*//") and
        # Also, class forward-declarations, namespace decls, and preprocessor
        # directives should not be documented.
        not reader.LookingAt(" *class +\\w+ *;") and
        (not reader.LookingAt(" *namespace +\\w+ *{") or
         reader.LookingAt(" *namespace +\\w+ *{}")) and
        not reader.LookingAt(" *#") and
        # And neither should inline method definitions declared outside
        # the class definition.
        not reader.LookingAt("[^ ].*\w+::\w+ *\\(\\)")):
    # Line contains content.  Transform the comment into a doc comment and
    # write it.
    FormatDocComment(comment, out)
  else:
    # No content.  The comment is not a doc comment.  Just write it.  But try
    # to make up some offset lines while we're here.
    if out.offset >= len(comment):
      # The comment is shorter than the offset, so don't write it at all.
      out.offset -= len(comment)
    else:
      out.writelines(comment[out.offset:])
      out.offset = 0

# Skips code lines until a comment block is encountered.
def FindNextComment(reader, out, group_tracker):
  while not reader.LookingAt(" *//") and not reader.Eof():
    if (reader.LookingAt(".*[^ \n].*$") and
        not reader.LookingAt(" *#")):
      # This line containts some sort of content.  End any groups which had
      # deeper indentation levels than this line.
      group_tracker.EndGroupsDeeperThan(reader.GetIndent(), out)

    # The following comment is obsolete now that we don't use Doxygen's
    # internal HTML rendering.  Our rendering handles sections correctly.
    #
    # By default, Doxygen puts class members into implicit groups like
    # "Public Member Functions" and "Public Fields".  This script sometimes
    # produces explicit groups as well, in order to improve organization.
    # Unfortunately, Doxygen does not handle things very well when some things
    # are explicitly grouped and some aren't.  In particular:
    # * If the SUBGROUPING option is on, then the explicit groups will be
    #   nested within the implicit ones, but *only if* all members of the group
    #   belong in the same implicit group (e.g. they are all methods, or all
    #   fields, etc.).  If you make an explicit group that crosses multiple
    #   implicit groups, the explicit group becomes a top-level group and
    #   sits along side the implicit ones.  This is really confusing, because
    #   it makes it look like these groups are different from the ones that
    #   are nested.
    # * If the SUBGROUPING option is off, then explicit groups always appear
    #   as peers to the implicit ones.  Unfortunately, the explicit groups
    #   seem to appear *above* the implicit ones, even if the members in the
    #   implicit group are all defined before the explicit group.
    #   Unfortunately, methods which are not in any particular group are almost
    #   always more important than methods that are in groups, and thus
    #   should appear first.
    # The only way we can solve both of these problems without giving up on
    # groups altogether is to force *all* members to be in explicit groups.
    # So, we start a group called "General Members" immediately after the
    # "public:" line to pick up ungrouped stuff.
#    is_public_line = False
#    public_line_indent = 0
#    if reader.LookingAt(" *public:"):
#      is_public_line = True
#      public_line_indent = reader.GetIndent() + 1

    if (reader.LookingAt(".*\\w.*//") and
        not reader.LookingAt(" *namespace ") and
        not reader.LookingAt(" *class +\\w+ *;") and
        not reader.LookingAt(" *#")):
      # This line has some content followed by a line comment, e.g.:
      #   FOO,       // The FOO enum value.
      # We want to convert the comment into a post-declaration comment, like:
      #   FOO,       ///< The FOO enum value.
      line = reader.Consume()
      out.write(line.replace("//", "///<", 1))
      # If subsequent lines have comments that are lined up with this one
      # and only whitespace before those comments, they must be part of this
      # comment.
      indent = line.index("//")
      while reader.LookingAt(" " * indent + "//"):
        out.write(reader.Consume().replace("//", "///<", 1))
    elif out.offset > 0 and reader.LookingAt(" *$"):
      # Skip empty line to make up an offset line.
      out.offset -= 1
      reader.Consume()
    elif reader.LookingAt(".*::google::protobuf::.*"):
      # Doxygen gets confused when it sees fully-qualified names that begin
      # with a "::".  As a result, if a function is defined as
      # "inline ::google::protobuf::Foo foo();", doxygen will decide that its
      # return type is "inline::google::protobuf::Foo".  The only place where
      # we use fully-qualified names in protobuf is in descriptor.pb.h, since
      # the protocol compiler defensively uses fully-qualified names
      # everywhere.  We can safely remove them, though.
      out.write(reader.Consume().replace("::google::protobuf::", " "))
    else:
      # Normal line; just print it.
      reader.ConsumeAndWrite(out)

    # See the monstrous comment earlier in this function.
#    if is_public_line:
#      group_tracker.StartGroup(public_line_indent, out)
#      out.write(" " * public_line_indent + "/// @name General Members\n\n")
#      out.offset += 2

# Read the entire file and convert it to Doxygen format.
def HandleFile(reader, filename, out):
  out = OffsetTrackingFile(out)

  # Skip copyright comment.
  while reader.LookingAt("//"):
    reader.ConsumeAndWrite(out)
  # Skip blank lines.
  while reader.LookingAt(" *$"):
    reader.ConsumeAndWrite(out)

  # Skip author comment
  if (reader.LookingAt("//.*Author")):
    while reader.LookingAt("// *[a-zA-Z0-9_]"):
      reader.ConsumeAndWrite(out)
    while reader.LookingAt("//$"):
      reader.ConsumeAndWrite(out)

  # Read the file-level docs.
  file_comment = []
  if (reader.LookingAt("//")):
    file_comment = ReadComment(reader)

  # Doxygen treats the file's description as if it is not inside any particular
  # namespace, and this it will not cross-link symbol names inside it.  To
  # get around this, we instead create a dummy class
  # "class [filename]_DoxygenScopedDocHack" and have the comments apply to
  # that.  It will then be properly cross-linked, and we can pull the
  # description off of it later in dox2gcode.cc.
  #
  # We need to make sure to define this inside the right namespace, of course.
  # It's too hard to look forward to find the namespace, so we derive it from
  # the filename instead.

  namespace_opener = None
  namespace_closer = None
  if filename.find("google/protobuf/io/") != -1:
    namespace_opener = "namespace google { namespace protobuf { namespace io {"
    namespace_closer = "} } }"
  elif filename.find("google/protobuf/compiler/cpp/") != -1:
    namespace_opener = "namespace google { namespace protobuf { namespace compiler { namespace cpp {"
    namespace_closer = "} } } }"
  elif filename.find("google/protobuf/compiler/java/") != -1:
    namespace_opener = "namespace google { namespace protobuf { namespace compiler { namespace java {"
    namespace_closer = "} } } }"
  elif filename.find("google/protobuf/compiler/python/") != -1:
    namespace_opener = "namespace google { namespace protobuf { namespace compiler { namespace python {"
    namespace_closer = "} } } }"
  elif filename.find("google/protobuf/compiler/ruby/") != -1:
    namespace_opener = "namespace google { namespace protobuf { namespace compiler { namespace ruby {"
    namespace_closer = "} } } }"
  elif filename.find("google/protobuf/compiler/objectivc/") != -1:
    namespace_opener = "namespace google { namespace protobuf { namespace compiler { namespace objectivc {"
    namespace_closer = "} } } }"
  elif filename.find("google/protobuf/compiler/js/") != -1:
    namespace_opener = "namespace google { namespace protobuf { namespace compiler { namespace js {"
    namespace_closer = "} } } }"
  elif filename.find("google/protobuf/compiler/javanano/") != -1:
    namespace_opener = "namespace google { namespace protobuf { namespace compiler { namespace javanano {"
    namespace_closer = "} } } }"
  elif filename.find("google/protobuf/compiler/") != -1:
    namespace_opener = "namespace google { namespace protobuf { namespace compiler {"
    namespace_closer = "} } }"
  elif filename.find("google/protobuf/") != -1:
    namespace_opener = "namespace google { namespace protobuf {"
    namespace_closer = "} }"

  filename_symbol = filename[filename.rindex('/')+1:]
  filename_symbol = filename_symbol.replace(".", "_").replace("-", "_")

  if namespace_opener:
    out.write(namespace_opener + "\n")
    out.offset += 1

  if file_comment:
    FormatDocComment(file_comment, out)
    out.write("class %s_DoxygenScopedDocHack {};\n" % filename_symbol)
    out.offset += 1

  # Even though we are putting our file-level docs on the ScopedDocHack,
  # we need to write an empty @file comment.  Otherwise, Doxygen will assume
  # that the file is undocumented, and it will make all links to the file
  # point at the file's source code listing instead.
  out.write("/// @file\n")
  out.offset += 1

  if namespace_closer:
    out.write(namespace_closer + "\n")
    out.offset += 1

  # Loop through the rest of the file.
  group_tracker = GroupTracker()
  while not reader.Eof():
    FindNextComment(reader, out, group_tracker)
    HandleComment(reader, out, group_tracker)

# Doxygen expects an input filter to take the source file name as a
# command-line argument and write the filtered text to stdout.
if len(sys.argv) == 1:
  # No arguments given.  Read from stdin.  Useful for debugging.
  HandleFile(Reader(sys.stdin), "stdin", sys.stdout)
else:
  for filename in sys.argv[1:]:
    f = file(filename, "r")
    try:
      HandleFile(Reader(f), filename, sys.stdout)
    finally:
      f.close()
