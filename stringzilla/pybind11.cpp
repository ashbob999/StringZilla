#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h> // `mmap`
#include <unistd.h>
#include <fcntl.h>    // `O_RDNNLY`

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "stringzilla.h"

namespace py = pybind11;

struct py_span_t;
struct py_str_t;
struct py_file_t;
struct py_subspan_t;
struct py_spans_t;

struct span_t {
    char const *ptr;
    size_t len;

    char const *data() const noexcept { return ptr; }
    size_t size() const noexcept { return len; }
};

static constexpr ssize_t ssize_max_k = std::numeric_limits<ssize_t>::max();
static constexpr size_t size_max_k = std::numeric_limits<size_t>::max();

inline size_t count_substr(span_t h_span, char n) noexcept {
    strzl_haystack_t h {h_span.ptr, h_span.len};

#if defined(__AVX2__)
    return strzl_avx2_count_char(h, n);
#elif defined(__ARM_NEON)
    return strzl_neon_count_char(h, n);
#else
    return strzl_naive_count_char(h, n);
#endif
}

inline size_t count_substr(span_t h_span, span_t n_span, bool overlap) noexcept {
    strzl_haystack_t h {h_span.ptr, h_span.len};
    strzl_needle_t n {n_span.ptr, n_span.len, 0};
    return strzl_naive_count_substr(h, n, overlap);
}

inline size_t find_substr(span_t h_span, char n) noexcept {
    strzl_haystack_t h {h_span.ptr, h_span.len};
    return strzl_naive_find_char(h, n);
}

inline size_t find_substr(span_t h_span, span_t n_span) noexcept {
    strzl_haystack_t h {h_span.ptr, h_span.len};
    strzl_needle_t n {n_span.ptr, n_span.len, 0};

#if defined(__AVX2__)
    return strzl_avx2_find_substr(h, n);
#elif defined(__ARM_NEON)
    return strzl_neon_find_substr(h, n);
#else
    return strzl_naive_find_substr(h, n);
#endif
}

span_t to_span(std::string_view s) { return {s.data(), s.size()}; }
std::string_view to_stl(span_t s) { return {s.data(), s.size()}; }

struct index_span_t {
    size_t offset;
    size_t length;
};

index_span_t unsigned_slice(size_t length, ssize_t start, ssize_t end) {
    if (start < 0 || end < 0) // TODO:
        throw std::invalid_argument("Negative slices aren't supported yet!");

    start = std::min(start, static_cast<ssize_t>(length));
    end = std::min(end, static_cast<ssize_t>(length));
    return {static_cast<size_t>(start), static_cast<size_t>(end - start)};
}

size_t unsigned_offset(size_t length, ssize_t idx) {
    if (idx >= 0) {
        if (static_cast<size_t>(idx) > length)
            throw std::out_of_range("Accessing beyond content length");
        return static_cast<size_t>(idx);
    }
    else {
        if (static_cast<size_t>(-idx) > length)
            throw std::out_of_range("Accessing beyond content length");
        return static_cast<size_t>(length + idx);
    }
}

span_t subspan(span_t span, ssize_t start, ssize_t end = ssize_max_k) {
    index_span_t index_span = unsigned_slice(span.size(), start, end);
    return {span.ptr + index_span.offset, index_span.length};
}

struct py_span_t : public span_t, public std::enable_shared_from_this<py_span_t> {

    py_span_t(span_t view = {}) : span_t(view) {}
    virtual ~py_span_t() {}

    using span_t::len;
    using span_t::ptr;

    span_t span() const { return {ptr, len}; }
    ssize_t size() const { return static_cast<ssize_t>(len); }
    bool contains(std::string_view needle, ssize_t start, ssize_t end) const;
    ssize_t find(std::string_view, ssize_t start, ssize_t end) const;
    ssize_t count(std::string_view, ssize_t start, ssize_t end, bool allowoverlap) const;
    std::shared_ptr<py_spans_t> splitlines(bool keeplinebreaks, char separator, size_t maxsplit) const;
    std::shared_ptr<py_spans_t> split(std::string_view separator, size_t maxsplit, bool keepseparator) const;
    std::shared_ptr<py_subspan_t> sub(ssize_t start, ssize_t end) const;

    char const *begin() const { return reinterpret_cast<char const *>(ptr); }
    char const *end() const { return begin() + len; }
    char at(ssize_t offset) const { return begin()[unsigned_offset(len, offset)]; }
    py::str to_python() const { return {begin(), len}; }

    span_t after_n(size_t offset) const noexcept {
        return (offset < len) ? span_t {ptr + offset, len - offset} : span_t {};
    }
    span_t before_n(size_t tail) const noexcept {
        return (tail < len) ? span_t {ptr + len - tail, len - tail} : span_t {};
    }
};

struct py_str_t : public py_span_t {
    std::string copy_;

    py_str_t(std::string string = "") : copy_(string) { ptr = to_span(copy_).ptr, len = to_span(copy_).len; }
    ~py_str_t() {}

    using py_span_t::contains;
    using py_span_t::count;
    using py_span_t::find;
    using py_span_t::size;
    using py_span_t::split;
    using py_span_t::splitlines;
};

struct py_file_t : public py_span_t {
  public:
    py_file_t(std::string path) {
        struct stat sb;
        int fd = open(path.c_str(), O_RDONLY);
        if (fstat(fd, &sb) != 0)
            throw std::runtime_error("Can't retrieve file size!");
        size_t file_size = sb.st_size;
        void *map = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED)
            throw std::runtime_error("Couldn't map the file!");
        ptr = reinterpret_cast<char const *>(map);
        len = file_size;
    }

    ~py_file_t() { munmap((void *)ptr, len); }

    using py_span_t::contains;
    using py_span_t::count;
    using py_span_t::find;
    using py_span_t::size;
    using py_span_t::split;
    using py_span_t::splitlines;
};

struct py_subspan_t : public py_span_t {
    std::shared_ptr<py_span_t const> parent_;

  public:
    py_subspan_t() = default;
    py_subspan_t(py_subspan_t &&) = default;
    py_subspan_t &operator=(py_subspan_t &&) = default;
    py_subspan_t(std::shared_ptr<py_span_t const> parent, span_t str) : parent_(std::move(parent)) {
        ptr = str.ptr, len = str.len;
    }

    using py_span_t::contains;
    using py_span_t::count;
    using py_span_t::find;
    using py_span_t::size;
    using py_span_t::split;
    using py_span_t::splitlines;
};

struct py_spans_t : public std::enable_shared_from_this<py_spans_t> {
    std::shared_ptr<py_span_t const> whole_;
    std::vector<span_t> parts_;

  public:
    py_spans_t() = default;
    py_spans_t(py_spans_t &&) = default;
    py_spans_t &operator=(py_spans_t &&) = default;
    py_spans_t(std::shared_ptr<py_span_t const> whole, std::vector<span_t> parts)
        : whole_(std::move(whole)), parts_(std::move(parts)) {}

    struct iterator_t {
        py_spans_t const *py_spans_ = nullptr;
        size_t idx_ = 0;

        bool operator==(iterator_t const &other) const { return idx_ == other.idx_; }
        bool operator!=(iterator_t const &other) const { return idx_ != other.idx_; }
        std::shared_ptr<py_subspan_t> operator*() const { return py_spans_->at(idx_); }
        iterator_t &operator++() {
            idx_++;
            return *this;
        }
        iterator_t operator++(int) {
            iterator_t old(*this);
            ++*this;
            return old;
        }
    };

    std::shared_ptr<py_subspan_t> at(ssize_t i) const {
        return std::make_shared<py_subspan_t>(whole_, parts_[unsigned_offset(size(), i)]);
    }

    std::shared_ptr<py_spans_t> sub(ssize_t start, ssize_t end) const {
        index_span_t index_span = unsigned_slice(parts_.size(), start, end);
        auto first_part_it = parts_.begin() + index_span.offset;
        std::vector<span_t> sub_parts(first_part_it, first_part_it + index_span.length);
        return std::make_shared<py_spans_t>(whole_, sub_parts);
    }

    iterator_t begin() const { return {this, 0}; }
    iterator_t end() const { return {this, parts_.size()}; }
    ssize_t size() const { return static_cast<ssize_t>(parts_.size()); }
};

bool py_span_t::contains(std::string_view needle, ssize_t start, ssize_t end) const {
    if (needle.size() == 0)
        return true;
    span_t part = subspan(span(), start, end);
    size_t offset = needle.size() == 1 //
                        ? find_substr(part, needle.front())
                        : find_substr(part, to_span(needle));
    return offset != part.len;
}

ssize_t py_span_t::find(std::string_view needle, ssize_t start, ssize_t end) const {
    if (needle.size() == 0)
        return 0;
    span_t part = subspan(span(), start, end);
    size_t offset = needle.size() == 1 //
                        ? find_substr(part, needle.front())
                        : find_substr(part, to_span(needle));
    return offset != part.len ? offset : -1;
}

ssize_t py_span_t::count(std::string_view needle, ssize_t start, ssize_t end, bool allowoverlap) const {
    if (needle.size() == 0)
        return 0;
    span_t part = subspan(span(), start, end);
    auto result = needle.size() == 1 //
                      ? count_substr(part, needle.front())
                      : count_substr(part, to_span(needle), allowoverlap);
    return result;
}

std::shared_ptr<py_spans_t> py_span_t::splitlines(bool keeplinebreaks, char separator, size_t maxsplit) const {

    size_t count_separators = count_substr(span(), separator);
    std::vector<span_t> parts(std::min(count_separators + 1, maxsplit));
    size_t last_start = 0;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        span_t remaining = after_n(last_start);
        size_t offset_in_remaining = find_substr(remaining, separator);
        parts[i] = span_t {ptr + last_start, offset_in_remaining + keeplinebreaks};
        last_start += offset_in_remaining + 1;
    }
    parts[count_separators] = after_n(last_start);
    return std::make_shared<py_spans_t>(shared_from_this(), std::move(parts));
}

std::shared_ptr<py_spans_t> py_span_t::split(std::string_view separator, size_t maxsplit, bool keepseparator) const {

    if (separator.size() == 1 && maxsplit == ssize_max_k)
        return splitlines(keepseparator, separator.front(), maxsplit);

    std::vector<span_t> parts;
    size_t last_start = 0;
    bool will_continue = true;
    while (last_start < len && parts.size() + 1 < maxsplit) {
        span_t remaining = after_n(last_start);
        size_t offset_in_remaining = find_substr(remaining, to_span(separator));
        will_continue = offset_in_remaining != remaining.size();
        size_t part_len = offset_in_remaining + separator.size() * keepseparator * will_continue;
        parts.emplace_back(span_t {remaining.data(), part_len});
        last_start += offset_in_remaining + separator.size();
    }
    // Python marks includes empy ending as well
    if (will_continue)
        parts.emplace_back(after_n(last_start));
    return std::make_shared<py_spans_t>(shared_from_this(), std::move(parts));
}

std::shared_ptr<py_subspan_t> py_span_t::sub(ssize_t start, ssize_t end) const {
    index_span_t index_span = unsigned_slice(size(), start, end);
    return std::make_shared<py_subspan_t>(shared_from_this(), span_t {ptr + index_span.offset, index_span.length});
}

template <typename at>
void define_slice_ops(py::class_<at, std::shared_ptr<at>> &str_view_struct) {

    str_view_struct.def( //
        "contains",
        &at::contains,
        py::arg("needle"),
        py::arg("start") = 0,
        py::arg("end") = ssize_max_k);
    str_view_struct.def( //
        "find",
        &at::find,
        py::arg("needle"),
        py::arg("start") = 0,
        py::arg("end") = ssize_max_k);
    str_view_struct.def( //
        "count",
        &at::count,
        py::arg("needle"),
        py::arg("start") = 0,
        py::arg("end") = ssize_max_k,
        py::arg("allowoverlap") = false);
    str_view_struct.def( //
        "splitlines",
        &at::splitlines,
        py::arg("keeplinebreaks") = false,
        py::arg("separator") = '\n',
        py::kw_only(),
        py::arg("maxsplit") = size_max_k);
    str_view_struct.def( //
        "split",
        &at::split,
        py::arg("separator") = " ",
        py::arg("maxsplit") = size_max_k,
        py::kw_only(),
        py::arg("keepseparator") = false);

    // Substring presence operator
    str_view_struct.def("__contains__",
                        [](at const &str, std::string_view needle) { return str.contains(needle, 0, ssize_max_k); });

    // Character access operators
    str_view_struct.def("__str__", &at::to_python);
    str_view_struct.def("__getitem__", &at::at, py::arg("index"));
    str_view_struct.def("__len__", &at::size);
    str_view_struct.def(
        "__iter__",
        [](at const &s) { return py::make_iterator(s.begin(), s.end()); },
        py::keep_alive<0, 1>());
}

PYBIND11_MODULE(stringzilla, m) {
    m.doc() = "Crunch 100+ GB Strings in Python with ease";

    auto py_span = py::class_<py_span_t, std::shared_ptr<py_span_t>>(m, "Span");
    define_slice_ops(py_span);

    auto py_subspan = py::class_<py_subspan_t, std::shared_ptr<py_subspan_t>>(m, "SubSpan");
    define_slice_ops(py_subspan);

    auto py_str = py::class_<py_str_t, std::shared_ptr<py_str_t>>(m, "Str");
    py_str.def(py::init([](std::string arg) { return std::make_shared<py_str_t>(std::move(arg)); }), py::arg("str"));
    define_slice_ops(py_str);

    auto py_file = py::class_<py_file_t, std::shared_ptr<py_file_t>>(m, "File");
    py_file.def( //
        py::init([](std::string path) { return std::make_shared<py_file_t>(std::move(path)); }),
        py::arg("path"));
    define_slice_ops(py_file);

    auto py_slices = py::class_<py_spans_t, std::shared_ptr<py_spans_t>>(m, "Slices");
    py_slices.def(py::init([]() { return std::make_shared<py_spans_t>(); }));
    py_slices.def("__len__", &py_spans_t::size);
    py_slices.def("__getitem__", &py_spans_t::at, py::arg("index"));
    py_slices.def(
        "__iter__",
        [](py_spans_t const &s) { return py::make_iterator(s.begin(), s.end()); },
        py::keep_alive<0, 1>());
}