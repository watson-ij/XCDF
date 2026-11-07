//! Python bindings for XCDF using pybind11.

#include <cstdio>
#include <limits>
#include <sstream>
#include <string>

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "xcdf/XCDFFieldDescriptor.h"
#include "xcdf/XCDFFile.h"
#include <xcdf/XCDFField.h>
#include <xcdf/version.h>

namespace py = pybind11;

std::string getVersion() {
  std::stringstream ss;
  ss << xcdf::get_version();
  return ss.str();
}

void write_test_file(const std::string &path) {
  XCDFFile f(path.c_str(), "w");

  XCDFUnsignedIntegerField field1 = f.AllocateUnsignedIntegerField("field1", 1);
  XCDFUnsignedIntegerField field2 =
      f.AllocateUnsignedIntegerField("field2", 1, "field1");
  XCDFFloatingPointField field3 = f.AllocateFloatingPointField("field3", 0.1);
  XCDFFloatingPointField field4 = f.AllocateFloatingPointField("field4", 0.1);
  XCDFFloatingPointField field5 = f.AllocateFloatingPointField("field5", 0.1);
  XCDFUnsignedIntegerField field6 = f.AllocateUnsignedIntegerField("field6", 1);
  XCDFFloatingPointField field7 = f.AllocateFloatingPointField("field7", 0.);
  // 2D vector
  XCDFUnsignedIntegerField field8 =
      f.AllocateUnsignedIntegerField("field8", 1, "field2");
  // 3D vector
  XCDFFloatingPointField field9 =
      f.AllocateFloatingPointField("field9", 0.5, "field8");
  // Header alias
  f.CreateAlias("testAlias", "field1 + 1");

  field1 << 2;
  field2 << 1 << 1;
  field3 << 0.1;
  field4 << 5.;
  field5 << 5.;
  field6 << 0xDEADBEEFDEADBEEFULL;
  field7 << 0.12;
  field8 << 2 << 1;
  field9 << 1. << 2. << 3.;

  f.Write();

  field1 << 2;
  field2 << 1 << 3;
  field3 << 0.3;

  // Write a NaN into field4
  field4 << std::numeric_limits<double>::signaling_NaN();

  // Write an inf into field5
  field5 << std::numeric_limits<double>::infinity();

  field6 << 0xDEADBEEFDEADBEEFULL;

  field7 << 0.12;

  field8 << 2 << 2 << 1 << 1;
  field9 << 1. << 2. << 3. << 4. << 5. << 6.;

  f.Write();

  for (int k = 0; k < 1000; k++) {
    field1 << 2;
    field2 << 1 << 3;
    field3 << 0.3;
    field4 << std::numeric_limits<double>::signaling_NaN();
    field5 << std::numeric_limits<double>::infinity();
    field6 << 0xDEADBEEFDEADBEEFULL;
    field7 << 0.12;
    field8 << 2 << 2 << 1 << 1;
    field9 << 1. << 2. << 3. << 4. << 5. << 6.;
    f.Write();
  }

  f.AddComment("test file");

  // Trailer alias
  f.CreateAlias("testTrailerAlias", "double(testAlias + 2)");
}

struct DictBuilder {
  py::dict data;

  template <typename T> void operator()(const XCDFField<T> &field) {

    py::str key = field.GetName();
    // return vector fields as numpy arrays
    if (field.HasParent()) {
      py::array_t<T> array(field.GetSize());
      py::buffer_info buffer = array.request();
      T *ptr = static_cast<T *>(buffer.ptr);

      for (size_t i = 0; i < field.GetSize(); i++) {
        ptr[i] = field[i];
      }

      data[key] = array;
      // scalar fields as python objects (None if not set)
    } else {
      if (field.GetSize() == 0) {
        data[key] = py::none();
      } else {
        data[key] = field[0];
      }
    }
  }
};


// Values of selected fields over many records, concatenated per field: the per-record dict of every field that
// ``__next__`` builds costs far more than the decode when a reader needs a few fields of thousands of records.
template <typename T>
struct Gathered {
  std::vector<T> values;
  std::vector<int64_t> counts;
};

template <typename T, typename F>
void gather_one(const F &field, Gathered<T> &g) {
  const size_t n = field.GetSize();
  for (size_t i = 0; i < n; i++) {
    g.values.push_back(field[i]);
  }
  g.counts.push_back(static_cast<int64_t>(n));
}

template <typename T>
py::tuple to_arrays(Gathered<T> &g) {
  py::array_t<T> v(g.values.size());
  std::copy(g.values.begin(), g.values.end(), static_cast<T *>(v.request().ptr));
  py::array_t<int64_t> c(g.counts.size());
  std::copy(g.counts.begin(), g.counts.end(), static_cast<int64_t *>(c.request().ptr));
  return py::make_tuple(v, c);
}

py::dict read_fields(XCDFFile &self, py::array_t<int64_t, py::array::c_style | py::array::forcecast> rows,
                     const std::vector<std::string> &names, int64_t max_gap) {
  auto r = rows.unchecked<1>();
  const py::ssize_t n = r.shape(0);
  std::vector<std::string> kind;  // "u", "i", "f" per name
  std::vector<Gathered<uint64_t>> gu(names.size());
  std::vector<Gathered<int64_t>> gi(names.size());
  std::vector<Gathered<double>> gf(names.size());
  for (const auto &name : names) {
    if (!self.HasField(name)) {
      throw py::key_error("no field " + name);
    }
    kind.push_back(self.IsUnsignedIntegerField(name) ? "u" : self.IsSignedIntegerField(name) ? "i" : "f");
  }
  {
    py::gil_scoped_release release;
    int64_t prev = -2;
    for (py::ssize_t k = 0; k < n; k++) {
      const int64_t row = r(k);
      if (prev >= 0 && row > prev + 1 && row - prev - 1 <= max_gap) {
        // a short gap: reading through the skipped records is cheaper than a seek
        for (int64_t s = prev + 1; s < row; s++) {
          if (self.Read() == 0) {
            py::gil_scoped_acquire acquire;
            throw py::index_error("no record at row " + std::to_string(s));
          }
        }
      } else if (row != prev + 1) {
        // the same positioning as the Python iterator: after seek(j) the next record read is j + 1
        bool ok = row == 0 ? self.Rewind() : self.Seek(static_cast<uint64_t>(row - 1));
        if (!ok) {
          py::gil_scoped_acquire acquire;
          throw py::index_error("cannot position before row " + std::to_string(row));
        }
      }
      if (self.Read() == 0) {
        py::gil_scoped_acquire acquire;
        throw py::index_error("no record at row " + std::to_string(row));
      }
      prev = row;
      for (size_t j = 0; j < names.size(); j++) {
        if (kind[j] == "u") {
          gather_one(self.GetUnsignedIntegerField(names[j]), gu[j]);
        } else if (kind[j] == "i") {
          gather_one(self.GetSignedIntegerField(names[j]), gi[j]);
        } else {
          gather_one(self.GetFloatingPointField(names[j]), gf[j]);
        }
      }
    }
  }
  py::dict out;
  for (size_t j = 0; j < names.size(); j++) {
    py::str key = names[j];
    out[key] = kind[j] == "u" ? to_arrays(gu[j]) : kind[j] == "i" ? to_arrays(gi[j]) : to_arrays(gf[j]);
  }
  return out;
}

PYBIND11_MODULE(xcdf, m) {

  m.attr("__version__") = getVersion();

  m.def("write_test_file", &write_test_file, "Write an XCDF test file in C++.");

  // XCDFUnsignedIntegerField
  py::class_<XCDFField<uint64_t>>(m, "XCDFUnsignedIntegerField",
                                  "Field with unsigned integer data.")
      .def("add",
           static_cast<void (XCDFField<uint64_t>::*)(uint64_t)>(
               &XCDFField<uint64_t>::Add),
           "Add a datum to the field.")
      .def("add",
           static_cast<void (XCDFField<uint64_t>::*)(
               const std::vector<uint64_t>)>(&XCDFField<uint64_t>::Add),
           "Add an array of data to the field.");

  // XCDFSignedIntegerField
  py::class_<XCDFField<int64_t>>(m, "XCDFSignedIntegerField",
                                 "Field with signed integer data.")
      .def("add",
           static_cast<void (XCDFField<int64_t>::*)(int64_t)>(
               &XCDFField<int64_t>::Add),
           "Add a datum to the field.")
      .def(
          "add",
          static_cast<void (XCDFField<int64_t>::*)(const std::vector<int64_t>)>(
              &XCDFField<int64_t>::Add),
          "Add an array of data to the field.");

  // XCDFFloatingPointField
  py::class_<XCDFField<double>>(m, "XCDFFloatingPointField",
                                "Field with floating point data.")
      .def("add",
           static_cast<void (XCDFField<double>::*)(double)>(
               &XCDFField<double>::Add),
           "Add a datum to the field.")
      .def("add",
           static_cast<void (XCDFField<double>::*)(const std::vector<double>)>(
               &XCDFField<double>::Add),
           "Add an array of data to the field.");

  py::class_<XCDFFieldDescriptor>(
      m, "FieldDescriptor", "Class that summarizes the properties of a field.")
      .def("__repr__",
           [](XCDFFieldDescriptor &self) {
             std::stringstream ss;
             ss << "FieldDescriptor(name=" << self.name_
                << ", type=" << static_cast<int>(self.type_)
                << ", parent=" << self.parentName_
                << ", raw_resolution=" << self.rawResolution_ << ")";

             return ss.str();
           })
      .def_property_readonly(
          "name", [](XCDFFieldDescriptor &self) { return self.name_; })
      .def_property_readonly(
          "type", [](XCDFFieldDescriptor &self) { return self.type_; })
      .def_property_readonly(
          "parent", [](XCDFFieldDescriptor &self) { return self.parentName_; })
      .def_property_readonly("raw_resolution", [](XCDFFieldDescriptor &self) {
        return self.rawResolution_;
      });

  py::class_<XCDFFile>(
      m, "File", "XCDF file handle with iterator access to stored records.")

      .def(py::init<const char *, const char *>(), py::arg("path"),
           py::arg("mode") = "r", R"pbdoc(
            Open a disk file in the specified mode.

            Parameters
            ----------
            path : `str`
                Path of the file on disk.
            mode : `str`
                Mode in which the file is opened.
                It defaults to 'r' which means open for reading in text mode.
                Other common values are 'w' for writing (truncating the file if it already exists),
                'x' for exclusive creation,
                and 'a' for appending
                (which on some Unix systems,
                means that all writes append to the end of the file regardless of
                the current seek position).
            )pbdoc")

      .def("add_comment", &XCDFFile::AddComment,
           "Add a string comment to the file")

      .def("allocate_float_field", &XCDFFile::AllocateFloatingPointField,
           R"pbdoc(
        Allocate a field with floating point data.
        
        Parameters
        ----------
        name : `str`
            Name of the field.
        resolution: `float`
            The floating point resolution needed in the field.
            If resolution <= 0. is specified, the full 64-bit
            double will be written to file.
        parent_name: `str`
            If the field is a vector, this is the name of the field that
            contains the number of entries in the vector.
        )pbdoc")

      .def("allocate_uint_field", &XCDFFile::AllocateUnsignedIntegerField,
           R"pbdoc(
        Allocate a field with unsigned integer data.
        
        Parameters
        ----------
        name : `str`
            Name of the field.
        resolution: `int`
            The (positive) integer resolution needed in the field
        parent_name: `str`
            If the field is a vector, this is the name of the field that
            contains the number of entries in the vector.
        )pbdoc")

      .def("allocate_int_field", &XCDFFile::AllocateSignedIntegerField,
           R"pbdoc(
        Allocate a field with signed integer data.
        
        Parameters
        ----------
        name : `str`
            Name of the field.
        resolution: `int`
            The (positive) integer resolution needed in the field
        parent_name: `str`
            If the field is a vector, this is the name of the field that
            contains the number of entries in the vector.
        )pbdoc")

      .def("close", &XCDFFile::Close, R"pbdoc(Close the File instance.
        
        Underlying file/stream resources will be closed
        and released unless the stream open/close constructors were invoked.
        Fields will be deallocated.
        If writing, do end checks to ensure all data is written out to file.

        )pbdoc")

      .def("write", &XCDFFile::Write,
           "Write one event to the uncompressed buffer.")

      .def("seek", &XCDFFile::Seek, py::arg("absolute event position"),
           "Seek to the given event in the file by absolute position.")
      .def("rewind", &XCDFFile::Rewind,
           "Return the file to a state where calling Read() gives the starting "
           "event, if possible.")
      .def("read_fields", &read_fields, py::arg("rows"), py::arg("names"), py::arg("max_gap") = 0,
           R"pbdoc(
        Read the given fields of the records at ``rows`` (absolute positions, any order; consecutive rows are read
        without seeking, and so are gaps of at most ``max_gap`` records, which are read and dropped).  Returns ``{name: (values, counts)}``: ``values`` the field's entries of every record
        concatenated (float64, uint64 or int64 by field type), ``counts`` [len(rows)] the entries per record
        (0 or 1 for a scalar field).  Leaves the file positioned after the last row read.
        )pbdoc")
      .def("__len__", &XCDFFile::GetEventCount,
           "Return the total number of events in the file.")
      .def("__in__", &XCDFFile::HasField,
           "Check if the file contains the given field.")
      .def(
          "check",
          [](XCDFFile &self) {
            while (self.Read()) { /* Do nothing */
            }
            self.Close();
          },
          "Check file by allow internal checksum verification to detect "
          "errors.")
      .def_property_readonly("version", &XCDFFile::GetVersion,
                             "Get the version number of the current open file.")
      .def_property_readonly(
          "n_fields", &XCDFFile::GetNFields,
          "Get the total number of fields allocated in the file.")
      .def_property_readonly("is_simple", &XCDFFile::IsSimple,
                             "Check if the underlying file has a trailer "
                             "pointer that can be seek to.")
      .def_property_readonly("is_open", &XCDFFile::IsOpen,
                             "Check if the file is currently open.")
      .def_property_readonly(
          "comments",
          [](XCDFFile &self) {
            std::vector<std::string> comments;
            comments.reserve(self.GetNComments());
            for (auto it = self.CommentsBegin(); it != self.CommentsEnd();
                 it++) {
              comments.push_back(*it);
            }
            return comments;
          },
          "Get the XCDF file comments (file header)")
      .def_property_readonly(
          "fields",
          [](XCDFFile &self) {
            std::vector<XCDFFieldDescriptor> fields;
            fields.reserve(self.GetNFields());
            for (auto it = self.FieldDescriptorsBegin();
                 it != self.FieldDescriptorsEnd(); it++) {
              fields.push_back(*it);
            }
            return fields;
          },
          "Get the fields contained in the file.")
      .def_property_readonly(
          "field_names",
          [](XCDFFile &self) {
            std::vector<std::string> names;
            names.reserve(self.GetNFields());
            for (auto field = self.FieldDescriptorsBegin();
                 field != self.FieldDescriptorsEnd(); field++) {
              names.push_back(field->name_);
            }
            return names;
          },
          "Get the list of field names contained in the file.")
      .def(
          "__iter__", [](XCDFFile &self) { return &self; },
          "Iterate along the file.")
      .def(
          "__next__",
          [](XCDFFile &self) {
            int ret = self.Read();
            if (ret == 0) {
              throw pybind11::stop_iteration();
            }
            DictBuilder builder;
            self.ApplyFieldVisitor(builder);
            return builder.data;
          },
          "Get to the next event.")
      .def(
          "__enter__", [](XCDFFile &self) { return &self; },
          "Enter the runtime context related to the file object")
      .def(
          "__exit__",
          [&](XCDFFile &r, void *exc_type, void *exc_value, void *traceback) {
            r.Close();
          },
          "Exit the runtime context related to this object.");
}
