/****************************************************************************
** Meta object code from reading C++ file 'postprocessing_dialog_qt.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <QtCore/qmetatype.h>
#include "postprocessing_dialog_qt.h"

#include <QtCore/qtmochelpers.h>

#include <memory>

#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'postprocessing_dialog_qt.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN2xe3app22PostProcessingDialogQtE_t {};
}  // unnamed namespace

template <>
constexpr inline auto xe::app::PostProcessingDialogQt::qt_create_metaobjectdata<
    qt_meta_tag_ZN2xe3app22PostProcessingDialogQtE_t>() {
  namespace QMC = QtMocConstants;
  QtMocHelpers::StringRefStorage qt_stringData{
      "xe::app::PostProcessingDialogQt",
      "OnAntiAliasingChanged",
      "",
      "index",
      "OnResamplingEffectChanged",
      "OnFsrSharpnessChanged",
      "value",
      "OnCasSharpnessChanged",
      "OnDitherChanged",
      "state",
      "OnResetFsrSharpness",
      "OnResetCasSharpness"};

  QtMocHelpers::UintData qt_methods{
      // Slot 'OnAntiAliasingChanged'
      QtMocHelpers::SlotData<void(int)>(1, 2, QMC::AccessPrivate,
                                        QMetaType::Void,
                                        {{
                                            {QMetaType::Int, 3},
                                        }}),
      // Slot 'OnResamplingEffectChanged'
      QtMocHelpers::SlotData<void(int)>(4, 2, QMC::AccessPrivate,
                                        QMetaType::Void,
                                        {{
                                            {QMetaType::Int, 3},
                                        }}),
      // Slot 'OnFsrSharpnessChanged'
      QtMocHelpers::SlotData<void(int)>(5, 2, QMC::AccessPrivate,
                                        QMetaType::Void,
                                        {{
                                            {QMetaType::Int, 6},
                                        }}),
      // Slot 'OnCasSharpnessChanged'
      QtMocHelpers::SlotData<void(int)>(7, 2, QMC::AccessPrivate,
                                        QMetaType::Void,
                                        {{
                                            {QMetaType::Int, 6},
                                        }}),
      // Slot 'OnDitherChanged'
      QtMocHelpers::SlotData<void(int)>(8, 2, QMC::AccessPrivate,
                                        QMetaType::Void,
                                        {{
                                            {QMetaType::Int, 9},
                                        }}),
      // Slot 'OnResetFsrSharpness'
      QtMocHelpers::SlotData<void()>(10, 2, QMC::AccessPrivate,
                                     QMetaType::Void),
      // Slot 'OnResetCasSharpness'
      QtMocHelpers::SlotData<void()>(11, 2, QMC::AccessPrivate,
                                     QMetaType::Void),
  };
  QtMocHelpers::UintData qt_properties{};
  QtMocHelpers::UintData qt_enums{};
  return QtMocHelpers::metaObjectData<
      PostProcessingDialogQt, qt_meta_tag_ZN2xe3app22PostProcessingDialogQtE_t>(
      QMC::MetaObjectFlag{}, qt_stringData, qt_methods, qt_properties,
      qt_enums);
}
Q_CONSTINIT const QMetaObject xe::app::PostProcessingDialogQt::staticMetaObject = { {
    QMetaObject::SuperData::link<QDialog::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN2xe3app22PostProcessingDialogQtE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN2xe3app22PostProcessingDialogQtE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN2xe3app22PostProcessingDialogQtE_t>.metaTypes,
    nullptr
} };

void xe::app::PostProcessingDialogQt::qt_static_metacall(QObject* _o,
                                                         QMetaObject::Call _c,
                                                         int _id, void** _a) {
  auto* _t = static_cast<PostProcessingDialogQt*>(_o);
  if (_c == QMetaObject::InvokeMetaMethod) {
    switch (_id) {
      case 0:
        _t->OnAntiAliasingChanged(
            (*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
        break;
      case 1:
        _t->OnResamplingEffectChanged(
            (*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
        break;
      case 2:
        _t->OnFsrSharpnessChanged(
            (*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
        break;
      case 3:
        _t->OnCasSharpnessChanged(
            (*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
        break;
      case 4:
        _t->OnDitherChanged(
            (*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
        break;
      case 5:
        _t->OnResetFsrSharpness();
        break;
      case 6:
        _t->OnResetCasSharpness();
        break;
      default:;
    }
  }
}

const QMetaObject* xe::app::PostProcessingDialogQt::metaObject() const {
  return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject()
                                    : &staticMetaObject;
}

void* xe::app::PostProcessingDialogQt::qt_metacast(const char* _clname) {
  if (!_clname) return nullptr;
  if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN2xe3app22PostProcessingDialogQtE_t>.strings))
    return static_cast<void*>(this);
  return QDialog::qt_metacast(_clname);
}

int xe::app::PostProcessingDialogQt::qt_metacall(QMetaObject::Call _c, int _id,
                                                 void** _a) {
  _id = QDialog::qt_metacall(_c, _id, _a);
  if (_id < 0) return _id;
  if (_c == QMetaObject::InvokeMetaMethod) {
    if (_id < 7) qt_static_metacall(this, _c, _id, _a);
    _id -= 7;
  }
  if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
    if (_id < 7) *reinterpret_cast<QMetaType*>(_a[0]) = QMetaType();
    _id -= 7;
  }
  return _id;
}
QT_WARNING_POP
