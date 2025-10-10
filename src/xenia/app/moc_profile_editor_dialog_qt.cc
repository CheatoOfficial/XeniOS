/****************************************************************************
** Meta object code from reading C++ file 'profile_editor_dialog_qt.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <QtCore/qmetatype.h>
#include "profile_editor_dialog_qt.h"

#include <QtCore/qtmochelpers.h>

#include <memory>

#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'profile_editor_dialog_qt.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.9.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN2xe3app21ProfileEditorDialogQtE_t {};
}  // unnamed namespace

template <>
constexpr inline auto xe::app::ProfileEditorDialogQt::qt_create_metaobjectdata<
    qt_meta_tag_ZN2xe3app21ProfileEditorDialogQtE_t>() {
  namespace QMC = QtMocConstants;
  QtMocHelpers::StringRefStorage qt_stringData{
      "xe::app::ProfileEditorDialogQt", "OnChangeIconClicked", "",
      "OnSaveClicked", "OnCancelClicked"};

  QtMocHelpers::UintData qt_methods{
      // Slot 'OnChangeIconClicked'
      QtMocHelpers::SlotData<void()>(1, 2, QMC::AccessPrivate, QMetaType::Void),
      // Slot 'OnSaveClicked'
      QtMocHelpers::SlotData<void()>(3, 2, QMC::AccessPrivate, QMetaType::Void),
      // Slot 'OnCancelClicked'
      QtMocHelpers::SlotData<void()>(4, 2, QMC::AccessPrivate, QMetaType::Void),
  };
  QtMocHelpers::UintData qt_properties{};
  QtMocHelpers::UintData qt_enums{};
  return QtMocHelpers::metaObjectData<
      ProfileEditorDialogQt, qt_meta_tag_ZN2xe3app21ProfileEditorDialogQtE_t>(
      QMC::MetaObjectFlag{}, qt_stringData, qt_methods, qt_properties,
      qt_enums);
}
Q_CONSTINIT const QMetaObject xe::app::ProfileEditorDialogQt::staticMetaObject = { {
    QMetaObject::SuperData::link<QDialog::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN2xe3app21ProfileEditorDialogQtE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN2xe3app21ProfileEditorDialogQtE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN2xe3app21ProfileEditorDialogQtE_t>.metaTypes,
    nullptr
} };

void xe::app::ProfileEditorDialogQt::qt_static_metacall(QObject* _o,
                                                        QMetaObject::Call _c,
                                                        int _id, void** _a) {
  auto* _t = static_cast<ProfileEditorDialogQt*>(_o);
  if (_c == QMetaObject::InvokeMetaMethod) {
    switch (_id) {
      case 0:
        _t->OnChangeIconClicked();
        break;
      case 1:
        _t->OnSaveClicked();
        break;
      case 2:
        _t->OnCancelClicked();
        break;
      default:;
    }
  }
  (void)_a;
}

const QMetaObject* xe::app::ProfileEditorDialogQt::metaObject() const {
  return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject()
                                    : &staticMetaObject;
}

void* xe::app::ProfileEditorDialogQt::qt_metacast(const char* _clname) {
  if (!_clname) return nullptr;
  if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN2xe3app21ProfileEditorDialogQtE_t>.strings))
    return static_cast<void*>(this);
  return QDialog::qt_metacast(_clname);
}

int xe::app::ProfileEditorDialogQt::qt_metacall(QMetaObject::Call _c, int _id,
                                                void** _a) {
  _id = QDialog::qt_metacall(_c, _id, _a);
  if (_id < 0) return _id;
  if (_c == QMetaObject::InvokeMetaMethod) {
    if (_id < 3) qt_static_metacall(this, _c, _id, _a);
    _id -= 3;
  }
  if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
    if (_id < 3) *reinterpret_cast<QMetaType*>(_a[0]) = QMetaType();
    _id -= 3;
  }
  return _id;
}
QT_WARNING_POP
