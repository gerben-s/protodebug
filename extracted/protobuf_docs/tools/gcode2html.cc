#include <iostream>
#include <string>
#include <map>
#include <utility>
#include <fstream>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <ctype.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/compiler/importer.h>
#include <ctemplate/template.h>
#include "googlecode-doc.pb.h"

using namespace std;
using namespace google::protobuf;
using namespace ctemplate;

// ===================================================================

void PopulateDictionary(const Message& message, TemplateDictionary* dict) {
  const Descriptor* descriptor = message.GetDescriptor();
  const Reflection* reflection = message.GetReflection();

  for (int i = 0; i < descriptor->field_count(); i++) {
    const FieldDescriptor* field = descriptor->field(i);
    string varname = descriptor->name() + "_" + field->name();

    if (field->cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE) {
      if (field->label() == FieldDescriptor::LABEL_REPEATED) {
        int size = reflection->FieldSize(message, field);
        if (size > 0) {
          dict->AddSectionDictionary(
            descriptor->name() + "_has_" + field->name());
        }
        for (int j = 0; j < size; j++) {
          TemplateDictionary* sub_dict = dict->AddSectionDictionary(varname);
          if (j == 0) {
            sub_dict->AddSectionDictionary(
              descriptor->name() + "_first_" + field->name());
          } else {
            sub_dict->AddSectionDictionary(
              descriptor->name() + "_not_first_" + field->name());
          }
          PopulateDictionary(reflection->GetRepeatedMessage(message, field, j),
                             sub_dict);
        }
      } else {
        if (reflection->HasField(message, field)) {
          PopulateDictionary(reflection->GetMessage(message, field),
                             dict->AddSectionDictionary(varname));
        } else {
          dict->AddSectionDictionary(
            descriptor->name() + "_missing_" + field->name());
        }
      }
    } else if (field->cpp_type() == FieldDescriptor::CPPTYPE_BOOL &&
               field->label() != FieldDescriptor::LABEL_REPEATED) {
      if (reflection->GetBool(message, field)) {
        dict->AddSectionDictionary(varname);
      } else {
        dict->AddSectionDictionary(
          descriptor->name() + "_not_" + field->name());
      }
    } else {
      if (field->label() == FieldDescriptor::LABEL_REPEATED) {
        int size = reflection->FieldSize(message, field);
        for (int j = 0; j < size; j++) {
          string value;
          if (field->cpp_type() == FieldDescriptor::CPPTYPE_STRING) {
            value = reflection->GetRepeatedString(message, field, j);
          } else {
            TextFormat::PrintFieldValueToString(message, field, j, &value);
          }

          dict->AddSectionDictionary(varname)->SetValue(varname, value);
        }
      } else {
        string value;
        if (field->cpp_type() == FieldDescriptor::CPPTYPE_STRING) {
          value = reflection->GetString(message, field);
        } else {
          TextFormat::PrintFieldValueToString(message, field, -1, &value);
        }

        if (field->label() == FieldDescriptor::LABEL_OPTIONAL) {
          if (reflection->HasField(message, field)) {
            dict->AddSectionDictionary(varname);
          } else {
            dict->AddSectionDictionary(
              descriptor->name() + "_missing_" + field->name());
          }
        }

        dict->SetValue(varname, value);
      }
    }
  }
}

// ===================================================================

enum ProtobufEncoding {
  ENCODING_TEXT,
  ENCODING_BINARY
};

void WriteFile(const string& contents, const string& filename) {
  cerr << "Writing: " << filename << endl;
  fstream out(filename.c_str(), ios::out);
  out.write(contents.data(), contents.size());
}

bool ReadFile(const string& filename, string* contents) {
  fstream in(filename.c_str(), ios::in);
  if (!in) {
    cerr << "Not found: " << filename << endl;
    return false;
  }
  cerr << "Reading: " << filename << endl;
  string line;
  while (getline(in, line)) {
    contents->append(line);
    contents->push_back('\n');
  }
  return true;
}

bool LoadFileWithIncludes(const string& filename, string* contents) {
  string raw_contents;
  if (!ReadFile(filename, &raw_contents)) return false;

  string::size_type pos = 0;
  while (true) {
    string::size_type old_pos = pos;
    pos = raw_contents.find("{{$", pos);
    contents->append(raw_contents, old_pos, pos - old_pos);
    if (pos == string::npos) break;

    pos += 3;
    string::size_type end = raw_contents.find("}}", pos);

    if (end == string::npos) {
      cerr << "Unterminated '{{$' include in " << filename << "." << endl;
      return false;
    }

    string include_file = raw_contents.substr(pos, end - pos);
    if (!LoadFileWithIncludes("tpl/" + include_file, contents)) {
      return false;
    }
    pos = end + 2;
  }

  return true;
}

Template* LoadTemplate(const string& filename) {
  string contents;
  if (!LoadFileWithIncludes(filename, &contents)) return NULL;
  if (filename.find("toc")!=string::npos){
    //Need to keep line breaks in .yaml file
    return Template::StringToTemplate(contents, ctemplate::STRIP_BLANK_LINES);
  } else {
    return Template::StringToTemplate(contents, ctemplate::STRIP_WHITESPACE);
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

bool Run(ProtobufEncoding input_encoding) {
  googlecode::Site site;

  {
    io::FileInputStream stdin(0);
    bool parsed = false;
    switch (input_encoding) {
      case ENCODING_TEXT:
        parsed = TextFormat::Parse(&stdin, &site);
        break;
      case ENCODING_BINARY:
        parsed = site.ParseFromZeroCopyStream(&stdin);
        break;
    }

    if (!parsed) {
      cerr << "Failed to parse input." << endl;
      return false;
    }
  }

  Template* cpp_api_tpl = LoadTemplate("tpl/index.tpl");
  Template* toc_tpl = LoadTemplate("tpl/toc.tpl");
  Template* file_tpl = LoadTemplate("tpl/file.tpl");

  if (cpp_api_tpl == NULL || file_tpl == NULL) {
    return false;
  }

  TemplateDictionary dict("index");
  PopulateDictionary(site, &dict);
  dict.SetValue("page_title", "C++ API");

  string output;
  if (!cpp_api_tpl->Expand(&output, &dict)) {
    cerr << "Failed to expand index.tpl." << endl;
    return false;
  }
  WriteFile(output, "gcode/cpp/index.html");

  output.clear();
  if (!toc_tpl->Expand(&output, &dict)) {
    cerr << "Failed to expand toc.tpl." << endl;
    return false;
  }
  WriteFile(output, "gcode/cpp/_cpptoc.yaml");

  for (int i = 0; i < site.package_size(); i++) {
    const googlecode::Package& package = site.package(i);
    for (int j = 0; j < package.file_size(); j++) {
      const googlecode::File& file = package.file(j);

      TemplateDictionary file_dict(file.name());
      PopulateDictionary(file, &file_dict);
      file_dict.SetValue("page_title", file.name());

      output.clear();
      if (!file_tpl->Expand(&output, &file_dict)) {
        cerr << "Failed to expand file.tpl." << endl;
        return false;
      }
      WriteFile(output, "gcode/cpp/" + file.url() + ".html");
    }
  }

  return true;
}

// ===================================================================

const char* program_name_ = NULL;

void Usage() {
  cerr << "Usage:" << endl;
  cerr << "  " << program_name_
       << " [--input=(text|binary)]"
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

  --argc;
  ++argv;
  while (argc > 0) {
    if (ParseArg(argv[0], "--input=", &encoding_name)) {
      input_encoding = ParseProtobufEncoding(encoding_name);
    } else {
      cerr << "Unknown argument: " << argv[0] << endl;
      Usage();
    }
    --argc;
    ++argv;
  }

  if (Run(input_encoding)) {
    return 0;
  } else {
    return 1;
  }
}
