//+----------------------------------------------------------------------------+
//| Description:  Magic Set Editor - Program to make Magic (tm) cards          |
//| Copyright:    (C) Twan van Laarhoven and the other MSE developers          |
//| License:      GNU General Public License 2 or later (see file COPYING)     |
//+----------------------------------------------------------------------------+

// ----------------------------------------------------------------------------- : Includes

#include <util/prec.hpp>
#include "reader.hpp"
#include <util/vector2d.hpp>
#include <util/error.hpp>
#include <util/io/package_manager.hpp>
#include <boost/logic/tribool.hpp>
#undef small
using boost::tribool;

// ----------------------------------------------------------------------------- : Reader

Reader::Reader(wxInputStream& input, Packaged* package, const String& filename, bool ignore_invalid)
  : indent(0), expected_indent(0), state(OUTSIDE)
  , ignore_invalid(ignore_invalid)
  , filename(filename), package(package), line_number(0), previous_line_number(0)
  , input(input)
{
  assert(input.IsOk());
  eat_utf8_bom(input);
  moveNext();
  handleAppVersion();
}

void Reader::handleIgnore(int end_version, const Char* a) {
  if (file_app_version < end_version) {
    if (enterBlock(a)) exitBlock();
  }
}

void Reader::handleAppVersion() {
   if (enterBlock(_("mse_version"))) {
    handle(file_app_version);
    if (app_version < file_app_version) {
      queue_message(MESSAGE_WARNING, _ERROR_2_("newer version", filename, file_app_version.toString()));
    }
    exitBlock();
  }
}

void Reader::warning(const String& msg, int line_number_delta, bool warn_on_previous_line) {
  warnings += String(_("\nOn line "))
           << ((warn_on_previous_line ? previous_line_number : line_number) + line_number_delta)
           << _(": \t") << msg;
}

void Reader::showWarnings() {
  if (!warnings.empty()) {
    queue_message(MESSAGE_WARNING, _("Warnings while reading file:\n") + filename + _("\n") + warnings);
    warnings.clear();
  }
}

bool Reader::enterAnyBlock() {
  if (state == ENTERED) moveNext(); // on the key of the parent block, first move inside it
  if (indent != expected_indent) return false; // not enough indentation
  state = ENTERED;
  expected_indent += 1; // the indent inside the block must be at least this much
  return true;
}

bool Reader::enterBlock(const Char* name) {
  if (state == ENTERED) moveNext(); // on the key of the parent block, first move inside it
  if (indent != expected_indent) return false; // not enough indentation
  if (key == name) {
    state = ENTERED;
    expected_indent += 1; // the indent inside the block must be at least this much
    return true;
  } else {
    return false;
  }
}

void Reader::exitBlock() {
  assert(expected_indent > 0);
  expected_indent -= 1;
  assert(state != UNHANDLED);
  previous_value.clear();
  if (state == ENTERED) moveNext(); // leave this key
  // Dump the remainder of the block
  // TODO: issue warnings?
  while (indent > expected_indent) {
    moveNext();
  }
  state = HANDLED;
}

void Reader::moveNext() {
  previous_line_number = line_number;
  state = HANDLED;
  key.clear();
  indent = -1; // if no line is read it never has the expected indentation
  // repeat until we have a good line
  while (key.empty() && !input.Eof()) {
    readLine();
  }
  // did we reach the end of the file?
  if (key.empty() && input.Eof()) {
    line_number += 1;
    indent = -1;
  }
}

/// Faster vector, uses large local storage
/** Usually a line from a file doesn't use very many characters.
 *  In that case, allocating a vector is a waste of resources.
 *  2007-09-21: Profiling => Using this class roughly halves the runtime of read_utf8_line,
 *              making startup slightly faster.
 */
template <typename T> class LocalVector {
public:
  LocalVector() : the_size(0), alloced(SMALL_SIZE), buffer(small) {}
  ~LocalVector() { if (buffer != small) free(buffer); }
  void push_back(T t) {
    if (the_size >= alloced) {
      // double buffer size
      if (buffer != small) {
        buffer = (T*)realloc(buffer, sizeof(T) * alloced * 2);
      } else {
        buffer = (T*)malloc(sizeof(T) * alloced * 2);
        memcpy(buffer, small, alloced * sizeof(T));
      }
      alloced *= 2;
    }
    buffer[the_size++] = t;
  }
  inline const T* get() const { return buffer; }
  inline size_t size() const { return the_size; }
private:
  static const int SMALL_SIZE = 1024;
  size_t the_size, alloced;
  T* buffer;
  T small[SMALL_SIZE];
};

/// Eat a utf-8 byte order mark from the begining of a stream
bool eat_utf8_bom(wxInputStream& input) {
  int c;
  if ((c = input.GetC()) == 0xEF) {
    if ((c = input.GetC()) == 0xBB) {
      if ((c = input.GetC()) == 0xBF) {
        return true;
      } else if (c != EOF) input.Ungetch(c);
    } else if (c != EOF) input.Ungetch(c);
  } else if (c != EOF) input.Ungetch(c);
  return false;
}

/// Read an UTF-8 encoded line from an input stream
/** As opposed to wx functions, this one actually reports errors
 */
String read_utf8_line(wxInputStream& input, bool until_eof = false);
String read_utf8_line(wxInputStream& input, bool until_eof) {
  LocalVector<char> buffer;
  while (true) {
    int c = input.GetC();
    if (c == EOF) break;
    if (!until_eof) {
      if (c == '\n') break;
      if (c == '\r') {
        c = input.GetC();
        if (c != '\n' && c != EOF) {
          input.Ungetch(c); // \r but not \r\n
        }
        break; 
      }
    }
    buffer.push_back((Byte)c);
  }
  // convert to string
  // TODO: Doing this in one step should be faster
  size_t size = wxConvUTF8.ToWChar(nullptr, 0, buffer.get(), buffer.size());
  if (size == size_t(-1)) {
    throw ParseError(_("Invalid UTF-8 sequence"));
  } else if (size == 0) {
    return _("");
  }
  return wxString::FromUTF8(buffer.get(), buffer.size());
}

void Reader::readLine(bool in_string) {
  line_number += 1;
  // We have to do our own line reading, because wxTextInputStream is insane
  try {
    line = read_utf8_line(input);
  } catch (const ParseError& e) {
    throw ParseError(e.what() + String(_(" on line ")) << line_number);
  }
  // read indentation
  indent = 0;
  while ((UInt)indent < line.size() && line.GetChar(indent) == _('\t')) {
    indent += 1;
  }
  // read key / value
  if (line.find_first_not_of(_(" \t")) == String::npos || line.GetChar(indent) == _('#')) {
    // empty line or comment
    key.clear();
    return;
  }
  size_t pos = line.find_first_of(_(':'), indent);
  key = line.substr(indent, pos - indent);
  if (!ignore_invalid && !in_string && starts_with(key, _(" "))) {
    warning(_("key: '") + key + _("' starts with a space; only use TABs for indentation!"), 0, false);
    // try to fix up: 8 spaces is a tab
    while (starts_with(key, _("        "))) {
      key = key.substr(8);
      indent += 1;
    }
  }
  key = canonical_name_form(trim(key));
  if (pos == String::npos) {
    if (!ignore_invalid && !in_string) {
      warning(_("Missing ':' "), 0, false);
    }
    value.clear();
  } else {
    value = trim_left(substr(line, pos+1));
  }
  if (key.empty() && pos!=String::npos) {
    key = _(" "); // we don't want an empty key if there was a colon
  }
}

void Reader::unknownKey() {
  // ignore?
  if (ignore_invalid) {
    do {
      moveNext();
    } while (indent > expected_indent);
    return;
  }
  if (indent >= expected_indent) {
    warning(_("Unexpected key: '") + key + _("'"), 0, false);
    do {
      moveNext();
    } while (indent > expected_indent);
  }
  // else: could be a nameless value, which doesn't call exitBlock to move past its own key
}

// ----------------------------------------------------------------------------- : Handling basic types

void Reader::unhandle() {
  assert(state == HANDLED);
  state = UNHANDLED;
}

const String& Reader::getValue() {
  assert(state != HANDLED); // don't try to handle things twice
  if (state == UNHANDLED) {
    state = HANDLED;
    return previous_value;
  } else if (value.empty()) {
    // a multiline string
    previous_value.clear();
    int pending_newlines = 0;
    // read all lines that are indented enough
    readLine(true);
    previous_line_number = line_number;
    while (indent >= expected_indent && !input.Eof()) {
      previous_value.resize(previous_value.size() + pending_newlines, _('\n'));
      pending_newlines = 0;
      previous_value += line.substr(expected_indent); // strip expected indent
      do {
        readLine(true);
        pending_newlines++;
        // skip empty lines that are not indented enough
      } while(trim(line).empty() && indent < expected_indent && !input.Eof());
    }
    // moveNext(), but without the initial readLine()
    state = HANDLED;
    while (key.empty() && !input.Eof()) {
      readLine();
    }
    // did we reach the end of the file?
    if (key.empty() && input.Eof()) {
      line_number += 1;
      indent = -1;
    }
    if (indent >= expected_indent) {
      warning(_("Blank line or comment in text block, that is insufficiently indented.\n")
              _("\t\tEither indent the comment/blank line, or add a 'key:' after it.\n")
              _("\t\tThis could cause more more error messages.\n"), -1, false);
    }
    return previous_value;
  } else {
    previous_value = value;
    moveNext();
    return previous_value;
  }
}

template <> void Reader::handle(String& s) {
  s = getValue();
}
template <> void Reader::handle(int& i) {
  long l = 0;
  if (!getValue().ToLong(&l)) {
    warning(_("Expected integer instead of '") + previous_value + _("'"));
  }
  i = l;
}
template <> void Reader::handle(unsigned int& i) {
  long l = 0;
  if (!getValue().ToLong(&l)) {
    warning(_("Expected non-negative integer instead of '") + previous_value + _("'"));
  } else if (l < 0) {
    warning(wxString::Format(_("Expected non-negative integer instead of %d"),(int)l));
  }
  i = abs(l); // abs, because it will seem strange if -1 comes out as MAX_INT
}
template <> void Reader::handle(double& d) {
  if (!getValue().ToDouble(&d)) {
    warning(_("Expected floating point number instead of '") + previous_value + _("'"));
  }
}
template <> void Reader::handle(bool& b) {
  const String& v = getValue();
  if (v==_("true") || v==_("1") || v==_("yes")) {
    b = true;
  } else if (v==_("false") || v==_("0") || v==_("no")) {
    b = false;
  } else {
    warning(_("Expected boolean ('true' or 'false') instead of '") + v + _("'"));
  }
}
template <> void Reader::handle(tribool& tb) {
  bool b;
  handle(b);
  tb = b;
}

// ----------------------------------------------------------------------------- : Handling less basic util types

template <> void Reader::handle(wxDateTime& date) {
  auto str = getValue();
  String::const_iterator end;
  if (!date.ParseDateTime(str,&end) || end != str.end()) {
    throw ParseError(_("Expected a date and time"));
  }
}

template <> void Reader::handle(Vector2D& vec) {
  if (!wxSscanf(getValue().c_str(), _("(%lf,%lf)"), &vec.x, &vec.y)) {
    throw ParseError(_("Expected (x,y)"));
  }
}

template <> void Reader::handle(LocalFileName& f) {
  f = LocalFileName::fromReadString(this->getValue());
}

// ----------------------------------------------------------------------------- : EnumReader

String EnumReader::notDoneErrorMessage() const {
  if (!first) throw InternalError(_("No first value in EnumReader"));
  return _ERROR_2_("unrecognized value", read, first);
}

void EnumReader::warnIfNotDone(Reader* errors_to) {
  if (!done) {
    // warning: unknown value
    errors_to->warning(notDoneErrorMessage());
  }
}

void EnumReader::errorIfNotDone() {
  if (!done) {
    throw ParseError(notDoneErrorMessage());
  }
}
