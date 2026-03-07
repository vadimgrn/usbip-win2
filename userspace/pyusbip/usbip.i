/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

%module usbip

%include <std_vector.i>
%include <std_string.i>
%include <windows.i>

%{
#include <vhci.h>
#include <remote.h>
#include <persistent.h>

#include <format>
%}

#define _In_
#define _Out_
#define _Inout_
#define _In_opt_
#define _Out_opt_
#define _Inout_opt_

// Typemap for bool& output parameter
%typemap(in, numinputs=0) bool &success (bool temp) {
  $1 = &temp;
}

%typemap(typecheck, precedence=SWIG_TYPECHECK_POINTER) bool &success {
  $1 = 1;  // Always match, this parameter is hidden from Python
}

%typemap(argout) bool &success {
  PyObject *obj = PyBool_FromLong(*$1);
  $result = SWIG_Python_AppendOutput($result, obj);
}

// Typemap to convert usbip::Handle to raw HANDLE
%typemap(in) HANDLE
{
  if (usbip::Handle *handle_ptr{}; auto ptype = SWIG_TypeQuery("usbip::Handle*")) {
    auto res = SWIG_ConvertPtr($input, (void**)&handle_ptr, ptype, 0);
    if (SWIG_IsOK(res) && handle_ptr) {
      $1 = handle_ptr->get<HANDLE>();
    } else {
      SWIG_exception_fail(SWIG_TypeError, "in method '" "$symname" "', argument " "$argnum"" of type '" "HANDLE""'");
    }
  } else {
    SWIG_exception_fail(SWIG_TypeError, "in method '" "$symname" "', argument " "$argnum"" of type '" "HANDLE""'");
  }
}

%typemap(out) usbip::Handle
{
  auto handle_ptr = new usbip::Handle();
  *handle_ptr = std::move($1);
  $result = SWIG_NewPointerObj(handle_ptr, SWIG_TypeQuery("usbip::Handle *"), SWIG_POINTER_OWN);
}

%typemap(out) usbip::Socket
{
  auto socket_ptr = new usbip::Socket();
  *socket_ptr = std::move($1);
  $result = SWIG_NewPointerObj(socket_ptr, SWIG_TypeQuery("usbip::Socket *"), SWIG_POINTER_OWN);
}

%include <dllspec.h>
%include <win_handle.h>
%include <win_socket.h>
%include <vhci.h>
%include <remote.h>
%include <persistent.h>

%template(vec_device_location) std::vector<usbip::device_location>;
%template(vec_imported_device) std::vector<usbip::imported_device>;

%extend usbip::device_location
{
    std::string __str__() const
    {
        return $self->hostname + ':' + $self->service + '/' + $self->busid;
    }
};

%extend usbip::imported_device
{
    std::string __str__() const
    {
        return std::format("{}, port {}, devid {}, '{}', '{}'",
               usbip_device_location___str__(&$self->location),
               $self->port, $self->devid, $self->vendor, $self->product);
    }
};
