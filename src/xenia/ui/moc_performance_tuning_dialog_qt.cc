/****************************************************************************
** Meta object code from reading C++ file 'performance_tuning_dialog_qt.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <QtCore/qmetatype.h>
#include "performance_tuning_dialog_qt.h"

#include <QtCore/qtmochelpers.h>

#include <memory>

#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error \
    "The header file 'performance_tuning_dialog_qt.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN2xe3app25PerformanceTuningDialogQtE_t {};
}  // unnamed namespace

template <>
constexpr inline auto
xe::app::PerformanceTuningDialogQt::qt_create_metaobjectdata<
    qt_meta_tag_ZN2xe3app25PerformanceTuningDialogQtE_t>() {
  namespace QMC = QtMocConstants;
  QtMocHelpers::StringRefStorage qt_stringData{
      "xe::app::PerformanceTuningDialogQt",
      "OnVsyncChanged",
      "",
      "state",
      "OnOcclusionQueryChanged",
      "OnReadbackResolveChanged",
      "value",
      "OnReadbackMemexportChanged",
      "OnClearMemoryPageStateChanged"};

  QtMocHelpers::UintData qt_methods{
      // Slot 'OnVsyncChanged'
      QtMocHelpers::SlotData<void(int)>(1, 2, QMC::AccessPrivate,
                                        QMetaType::Void,
                                        {{
                                            {QMetaType::Int, 3},
                                        }}),
      // Slot 'OnOcclusionQueryChanged'
      QtMocHelpers::SlotData<void(int)>(4, 2, QMC::AccessPrivate,
                                        QMetaType::Void,
                                        {{
                                            {QMetaType::Int, 3},
                                        }}),
      // Slot 'OnReadbackResolveChanged'
      QtMocHelpers::SlotData<void(int)>(5, 2, QMC::AccessPrivate,
                                        QMetaType::Void,
                                        {{
                                            {QMetaType::Int, 6},
                                        }}),
      // Slot 'OnReadbackMemexportChanged'
      QtMocHelpers::SlotData<void(int)>(7, 2, QMC::AccessPrivate,
                                        QMetaType::Void,
                                        {{
                                            {QMetaType::Int, 6},
                                        }}),
      // Slot 'OnClearMemoryPageStateChanged'
      QtMocHelpers::SlotData<void(int)>(8, 2, QMC::AccessPrivate,
                                        QMetaType::Void,
                                        {{
                                            {QMetaType::Int, 3},
                                        }}),
  };
  QtMocHelpers::UintData qt_properties{};
  QtMocHelpers::UintData qt_enums{};
  return QtMocHelpers::metaObjectData<
      PerformanceTuningDialogQt,
      qt_meta_tag_ZN2xe3app25PerformanceTuningDialogQtE_t>(
      QMC::MetaObjectFlag{}, qt_stringData, qt_methods, qt_properties,
      qt_enums);
}
Q_CONSTINIT const QMetaObject xe::app::PerformanceTuningDialogQt::staticMetaObject = { {
    QMetaObject::SuperData::link<ui::GamepadDialog::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN2xe3app25PerformanceTuningDialogQtE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN2xe3app25PerformanceTuningDialogQtE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN2xe3app25PerformanceTuningDialogQtE_t>.metaTypes,
    nullptr
} };

void xe::app::PerformanceTuningDialogQt::qt_static_metacall(
    QObject* _o, QMetaObject::Call _c, int _id, void** _a) {
  auto* _t = static_cast<PerformanceTuningDialogQt*>(_o);
  if (_c == QMetaObject::InvokeMetaMethod) {
    switch (_id) {
      case 0:
        _t->OnVsyncChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
        break;
      case 1:
        _t->OnOcclusionQueryChanged(
            (*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
        break;
      case 2:
        _t->OnReadbackResolveChanged(
            (*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
        break;
      case 3:
        _t->OnReadbackMemexportChanged(
            (*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
        break;
      case 4:
        _t->OnClearMemoryPageStateChanged(
            (*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
        break;
      default:;
    }
  }
}

const QMetaObject* xe::app::PerformanceTuningDialogQt::metaObject() const {
  return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject()
                                    : &staticMetaObject;
}

void* xe::app::PerformanceTuningDialogQt::qt_metacast(const char* _clname) {
  if (!_clname) return nullptr;
  if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN2xe3app25PerformanceTuningDialogQtE_t>.strings))
    return static_cast<void*>(this);
  return ui::GamepadDialog::qt_metacast(_clname);
}

int xe::app::PerformanceTuningDialogQt::qt_metacall(QMetaObject::Call _c,
                                                    int _id, void** _a) {
  _id = ui::GamepadDialog::qt_metacall(_c, _id, _a);
  if (_id < 0) return _id;
  if (_c == QMetaObject::InvokeMetaMethod) {
    if (_id < 5) qt_static_metacall(this, _c, _id, _a);
    _id -= 5;
  }
  if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
    if (_id < 5) *reinterpret_cast<QMetaType*>(_a[0]) = QMetaType();
    _id -= 5;
  }
  return _id;
}
QT_WARNING_POP
