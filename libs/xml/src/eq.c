#include "core_diag.h"
#include "xml_doc.h"
#include "xml_eq.h"

static bool xml_eq_elem(XmlDoc* doc, const XmlNode x, const XmlNode y) {
  // Verify node names.
  if (!string_eq(xml_name(doc, x), xml_name(doc, y))) {
    return false;
  }

  // Verify attributes.
  XmlNode attrX = xml_first_attr(doc, x);
  XmlNode attrY = xml_first_attr(doc, y);
  do {
    if (!xml_eq(doc, attrX, attrY)) {
      return false;
    }
    attrX = xml_next(doc, attrX);
    attrY = xml_next(doc, attrY);
  } while (!sentinel_check(attrX) && !sentinel_check(attrY));

  // Verify children.
  XmlNode childX = xml_first_child(doc, x);
  XmlNode childY = xml_first_child(doc, y);
  do {
    if (!xml_eq(doc, childX, childY)) {
      return false;
    }
    childX = xml_next(doc, childX);
    childY = xml_next(doc, childY);
  } while (!sentinel_check(childX) && !sentinel_check(childY));

  return true;
}

bool xml_eq(XmlDoc* doc, const XmlNode x, const XmlNode y) {
  if (sentinel_check(x) && sentinel_check(y)) {
    return true;
  }
  if (sentinel_check(x) || sentinel_check(y)) {
    return false;
  }
  const XmlType type = xml_type(doc, x);
  if (type != xml_type(doc, y)) {
    return false;
  }
  switch (type) {
  case XmlType_Element:
    return xml_eq_elem(doc, x, y);
  case XmlType_Attribute:
    return string_eq(xml_name(doc, x), xml_name(doc, y)) &&
           string_eq(xml_value(doc, x), xml_value(doc, y));
  case XmlType_Text:
  case XmlType_Comment:
    return string_eq(xml_value(doc, x), xml_value(doc, y));
  case XmlType_Count:
    break;
  }
  diag_crash();
}
