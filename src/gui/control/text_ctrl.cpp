//+----------------------------------------------------------------------------+
//| Description:  Magic Set Editor - Program to make Magic (tm) cards          |
//| Copyright:    (C) Twan van Laarhoven and the other MSE developers          |
//| License:      GNU General Public License 2 or later (see file COPYING)     |
//+----------------------------------------------------------------------------+

// ----------------------------------------------------------------------------- : Includes

#include <util/prec.hpp>
#include <gui/control/text_ctrl.hpp>
#include <gui/value/editor.hpp>
#include <gui/util.hpp>
#include <data/field/text.hpp>
#include <data/action/value.hpp>

// ----------------------------------------------------------------------------- : TextCtrl

TextCtrl::TextCtrl(Window* parent, int id, bool multi_line, long style)
  : DataEditor(parent, id, style)
  , multi_line(multi_line)
{}
TextCtrl::~TextCtrl() {}

Rotation TextCtrl::getRotation() const {
  return Rotation(0, RealRect(RealPoint(0,0),GetClientSize()));
}

void TextCtrl::draw(DC& dc) {
  RotatedDC rdc(dc, getRotation(), QUALITY_LOW);
  if (viewers.empty() || !static_cast<FakeTextValue&>(*viewers.front()->getValue()).editable) {
    DataViewer::draw(rdc, wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE));
  } else {
    DataViewer::draw(rdc, wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
  }
}

bool TextCtrl::AcceptsFocus() const {
  return wxWindow::AcceptsFocus() && 
         !viewers.empty() &&
         static_cast<FakeTextValue&>(*viewers.front()->getValue()).editable;
}


TextStyle& TextCtrl::getStyle() {
  assert(!viewers.empty());
  return static_cast<TextStyle&>(*viewers.front()->getStyle());
}
TextField& TextCtrl::getField() {
  assert(!viewers.empty());
  return static_cast<TextField&>(*viewers.front()->getField());
}
TextFieldP TextCtrl::getFieldP() {
  assert(!viewers.empty());
  return static_pointer_cast<TextField>(viewers.front()->getField());
}
void TextCtrl::updateSize() {
  wxSize cs = GetClientSize();
  ValueViewer& viewer = *viewers.front();
  viewer.bounding_box.width  = cs.GetWidth()  - 2;
  viewer.bounding_box.height = cs.GetHeight() - 2;
  viewers.front()->getEditor()->determineSize(true);
}

void TextCtrl::setValue(String* value, bool untagged) {
  setValue(make_intrusive<FakeTextValue>(getFieldP(), value, true, untagged));
}
void TextCtrl::setValue(const FakeTextValueP& value) {
  value->retrieve();
  viewers.front()->setValue(value);
  updateSize();
  onChange();
}

void TextCtrl::onChangeSet() {
  DataEditor::onChangeSet();
  // initialize
  if (viewers.empty()) {
    // create a field, style and value
    TextFieldP field = make_intrusive<TextField>();
    TextStyleP style = make_intrusive<TextStyle>(field);
    TextValueP value = make_intrusive<FakeTextValue>(field, nullptr, false, false);
    // set stuff
    field->index = 0;
    field->multi_line = multi_line;
    style->width = 100;
    style->height = 20;
    style->left = 1;
    style->top = 1;
    style->font.color = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    // assign to this control
    IndexMap<FieldP,StyleP> styles; styles.add(field, style);
    IndexMap<FieldP,ValueP> values; values.add(field, value);
    setStyles(set->stylesheet, styles);
    setData(values);
    updateSize();
    onChange();
  } else {
    setValue(nullptr);
  }
  // select the one and only editor
  current_viewer = viewers.front().get();
  current_editor = current_viewer->getEditor();
}

void TextCtrl::onInit() {
  // Give viewers a chance to show/hide controls (scrollbar) when selecting other editors
  FOR_EACH_EDITOR {
    e->onShow(true);
  }
  // also init the DataEditor
  DataEditor::onInit();
}

void TextCtrl::onSize(wxSizeEvent&) {
  if (!viewers.empty()) {
    updateSize();
    onChange();
  }
}
wxSize TextCtrl::DoGetBestSize() const {
  if (multi_line || viewers.empty()) {
    // flexible size
    return wxSize(1,1);
  } else {
    wxSize ws = GetSize(), cs = GetClientSize();
    ValueViewer& viewer = *viewers.front();
    return wxSize(viewer.bounding_box.width, viewer.bounding_box.height) + ws - cs;
  }
}

BEGIN_EVENT_TABLE(TextCtrl, DataEditor)
  EVT_SIZE        (TextCtrl::onSize)
END_EVENT_TABLE()
