// Copyright 2018-2025 Admenri.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "binding/mri/mri_win32api.h"

#include "binding/mri/mri_util.h"
#include "base/debug/logging.h"

#include <mutex>
#include <unordered_set>

namespace binding {

namespace {

struct Win32APIInfo {
  std::string dll_name;
  std::string proc_name;
  std::string import_signature;
  std::string return_signature;
};

std::string BuildAPIKey(const Win32APIInfo& info) {
  return info.dll_name + "!" + info.proc_name;
}

bool ShouldLogOnce(std::unordered_set<std::string>& logged_set,
                   const std::string& key) {
  static std::mutex logged_mutex;
  std::lock_guard<std::mutex> lock(logged_mutex);
  return logged_set.insert(key).second;
}

std::unordered_set<std::string> g_logged_bind_apis;
std::unordered_set<std::string> g_logged_call_apis;

void Win32APIFreeInstance(void* ptr) {
  delete static_cast<Win32APIInfo*>(ptr);
}

MRI_DEFINE_DATATYPE(Win32API, "Win32API", Win32APIFreeInstance);

MRI_METHOD(win32api_initialize) {
  VALUE rb_dll_name = Qnil;
  VALUE rb_proc_name = Qnil;
  VALUE rb_import_signature = Qnil;
  VALUE rb_return_signature = Qnil;
  rb_scan_args(argc, argv, "22", &rb_dll_name, &rb_proc_name,
               &rb_import_signature, &rb_return_signature);

  auto* info = new Win32APIInfo;
  info->dll_name = MRI_FROM_STRING(rb_obj_as_string(rb_dll_name));
  info->proc_name = MRI_FROM_STRING(rb_obj_as_string(rb_proc_name));
  info->import_signature = NIL_P(rb_import_signature)
                               ? "nil"
                               : MRI_FROM_STRING(rb_obj_as_string(rb_import_signature));
  info->return_signature = NIL_P(rb_return_signature)
                               ? "L"
                               : MRI_FROM_STRING(rb_obj_as_string(rb_return_signature));
  MriSetStructData(self, info);

  const std::string key = BuildAPIKey(*info);
  if (ShouldLogOnce(g_logged_bind_apis, key)) {
    LOG(WARNING) << "[Binding] Win32API bind: " << info->dll_name << "!"
                 << info->proc_name << " import=" << info->import_signature
                 << " return=" << info->return_signature;
  }
  return self;
}

MRI_METHOD(win32api_call) {
  auto* info = MriGetStructData<Win32APIInfo>(self);
  const std::string key = BuildAPIKey(*info);
  if (ShouldLogOnce(g_logged_call_apis, key)) {
    LOG(WARNING) << "[Binding] Win32API call: " << info->dll_name << "!"
                 << info->proc_name << " argc=" << argc;
  }

  // Stub return value for compatibility-only probing.
  return INT2NUM(0);
}

}  // namespace

void InitWin32APIBinding() {
  ID win32api_id = rb_intern("Win32API");
  if (rb_const_defined(rb_cObject, win32api_id))
    return;

  VALUE klass = rb_define_class("Win32API", rb_cObject);
  rb_define_alloc_func(klass, MriClassAllocate<&kWin32APIDataType>);
  MriDefineMethod(klass, "initialize", win32api_initialize);
  MriDefineMethod(klass, "call", win32api_call);
  rb_define_alias(klass, "Call", "call");
}

}  // namespace binding
