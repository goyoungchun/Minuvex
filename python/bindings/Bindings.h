#pragma once
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>
#include <minvex/Viewer.h>

namespace py = pybind11;
namespace mvxpy {
struct Failure : std::runtime_error {
    explicit Failure(lv::Error error) : std::runtime_error(error.Message), Detail(std::move(error)) {}
    lv::Error Detail;
};
template<class T> T Check(lv::Result<T> result) {
    if (!result) {throw Failure(std::move(result).Error());}
    return std::move(result).Value();
}
inline void Check(lv::Result<void> result) {if (!result) {throw Failure(std::move(result).Error());}}
void BindValues(py::module_& module);
}
