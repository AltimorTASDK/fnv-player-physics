#pragma once

#include "util/meta.h"
#include "util/operators.h"
#include <cmath>
#include <cstdint>
#include <utility>
#include <tuple>
#include <type_traits>

template<typename Base>
class vec_impl;

template<typename T>
concept VecImpl = requires(T t) { []<typename U>(vec_impl<U>){}(t); };

template<typename Base>
class vec_impl : public Base {
	template<typename OtherBase>
	friend class vec_impl;

	using elem_tuple = decltype(std::declval<const Base>().elems());
	using elem_type  = std::tuple_element_t<0, elem_tuple>;

	static constexpr auto elem_count = sizeof_tuple<elem_tuple>;

public:
	using Base::elems;

	static constexpr elem_type dot(const vec_impl &a, const vec_impl &b)
	{
		return sum_tuple(a.foreach(operators::mul, b.elems()));
	}

	// Component-wise min of two vectors
	static constexpr vec_impl min(const vec_impl &a, const vec_impl &b)
	{
		return a.map(operators::min, b.elems());
	}

	// Component-wise max of two vectors
	static constexpr vec_impl max(const vec_impl &a, const vec_impl &b)
	{
		return a.map(operators::max, b.elems());
	}

	// Component-wise min and max of two vectors
	static constexpr auto min_max(const vec_impl &a, const vec_impl &b)
	{
		return std::make_pair(min(a, b), max(a, b));
	}

	// Component-wise lerp of two vectors
	static constexpr vec_impl lerp(const vec_impl &a, const vec_impl &b, auto t)
	{
		return a.map(::bind_back(std::lerp, t), b.elems());
	}

	constexpr vec_impl()
	{
		foreach(::bind_back(operators::eq, elem_type{}));
	}

	constexpr vec_impl(const vec_impl &other)
	{
		*this = other;
	}

	template<typename ...T> requires(sizeof...(T) == elem_count)
	constexpr vec_impl(T ...values)
	{
		elems() = std::make_tuple((elem_type)values...);
	}

	explicit constexpr vec_impl(elem_tuple &&tuple)
	{
		elems() = tuple;
	}

	template<typename OtherBase>
	explicit constexpr vec_impl(const vec_impl<OtherBase> &other)
	{
		if constexpr (elem_count > other.elem_count) {
			constexpr auto pad = elem_count - other.elem_count;
			elems() = std::tuple_cat(other.elems(), fill_tuple<pad>(elem_type{}));
		} else if constexpr (elem_count < other.elem_count) {
			elems() = slice_tuple<0, elem_count>(other.elems());
		} else {
			elems() = other.elems();
		}
	}

	template<size_t N>
	constexpr auto &get() { return std::get<N>(elems()); }

	template<size_t N>
	constexpr auto get() const { return std::get<N>(elems()); }

	template<FixedTuple<elem_count> ...Tuples>
	constexpr auto foreach(auto &&callable, Tuples &&...tuples)
	{
		return zip_apply(callable, elems(), tuples...);
	}

	template<FixedTuple<elem_count> ...Tuples>
	constexpr auto foreach(auto &&callable, Tuples &&...tuples) const
	{
		return zip_apply(callable, elems(), tuples...);
	}

	constexpr auto length_sqr() const
	{
		return dot(*this, *this);
	}

	constexpr auto length() const
	{
		return std::sqrt(length_sqr());
	}

	constexpr auto normalized() const
	{
		const auto len = length();
		if (len != 0)
			return *this * (decltype(len) { 1 } / len);
		else
			return *this;
	}

	// Create a new vector by applying a function to each component
	template<VecImpl T = vec_impl>
	constexpr T map(auto &&callable, auto &&...tuples) const
	{
		return T(foreach(callable, tuples...));
	}

	// Component-wise absolute value
	constexpr vec_impl abs() const
	{
		return map(operators::abs);
	}

	constexpr vec_impl &operator=(const vec_impl &other)
	{
		elems() = other.elems();
		return *this;
	}

	constexpr vec_impl &operator+=(const vec_impl &other)
	{
		foreach(operators::add_eq, other.elems());
		return *this;
	}

	constexpr vec_impl operator+(const vec_impl &other) const
	{
		return map(operators::add, other.elems());
	}

	constexpr vec_impl &operator-=(const vec_impl &other)
	{
		foreach(operators::sub_eq, other.elems());
		return *this;
	}

	constexpr vec_impl operator-(const vec_impl &other) const
	{
		return map(operators::sub, other.elems());
	}

	constexpr vec_impl &operator*=(const vec_impl &other)
	{
		foreach(operators::mul_eq, other.elems());
		return *this;
	}

	constexpr vec_impl &operator*=(elem_type value)
	{
		foreach(::bind_back(operators::mul_eq, value));
		return *this;
	}

	constexpr vec_impl operator*(const vec_impl &other) const
	{
		return map(operators::mul, other.elems());
	}

	constexpr vec_impl operator*(elem_type value) const
	{
		return map(::bind_back(operators::mul, value));
	}

	constexpr vec_impl &operator/=(const vec_impl &other)
	{
		foreach(operators::div_eq, other.elems());
		return *this;
	}

	constexpr vec_impl &operator/=(elem_type value)
	{
		foreach(::bind_back(operators::div_eq, value));
		return *this;
	}

	constexpr vec_impl operator/(const vec_impl &other) const
	{
		return map(operators::div, other.elems());
	}

	constexpr vec_impl operator/(elem_type value) const
	{
		return map(::bind_back(operators::div, value));
	}

	constexpr bool operator==(const vec_impl &other) const
	{
		return elems() == other.elems();
	}

	constexpr vec_impl operator-() const
	{
		return map(operators::neg);
	}
};

template<typename T>
struct vec2_base {
	T x, y;
	constexpr auto elems()       { return std::tie(x, y); }
	constexpr auto elems() const { return std::make_tuple(x, y); }
};

using vec2 = vec_impl<vec2_base<float>>;

template<typename T>
struct vec3_base {
	T x, y, z;
	constexpr auto elems() { return std::tie(x, y, z); }
	constexpr auto elems() const { return std::make_tuple(x, y, z); }

	static constexpr vec_impl<vec3_base> cross(const auto &a, const auto &b)
	{
		return vec_impl<vec3_base>(
			a.y * b.z - a.z * b.y,
			a.z * b.x - a.x * b.z,
			a.x * b.y - a.y * b.x);
	}
};

using vec3 = vec_impl<vec3_base<float>>;

template<typename T>
struct vec4_base {
	T x, y, z, w;
	constexpr auto elems()       { return std::tie(x, y, z, w); }
	constexpr auto elems() const { return std::make_tuple(x, y, z, w); }
};

using vec4 = vec_impl<vec4_base<float>>;

template<typename T, T MaxValue>
struct color_rgb_base {
	static constexpr auto hex(uint32_t value)
	{
		const auto r = (T)(((value >> 16) & 0xFF) * MaxValue / 255);
		const auto g = (T)(((value >>  8) & 0xFF) * MaxValue / 255);
		const auto b = (T)(((value >>  0) & 0xFF) * MaxValue / 255);
		return vec_impl<color_rgb_base>(r, g, b);
	}

	T r, g, b;
	constexpr auto elems()       { return std::tie(r, g, b); }
	constexpr auto elems() const { return std::make_tuple(r, g, b); }
};

using color_rgb_u8  = vec_impl<color_rgb_base<uint8_t, 255>>;
using color_rgb_s16 = vec_impl<color_rgb_base<int16_t, 255>>;
using color_rgb_f32 = vec_impl<color_rgb_base<float, 1.0f>>;

template<typename T, T MaxValue>
struct color_rgba_base {
	static constexpr auto hex(uint32_t value)
	{
		const auto r = (T)(((value >> 24) & 0xFF) * MaxValue / 255);
		const auto g = (T)(((value >> 16) & 0xFF) * MaxValue / 255);
		const auto b = (T)(((value >>  8) & 0xFF) * MaxValue / 255);
		const auto a = (T)(((value >>  0) & 0xFF) * MaxValue / 255);
		return vec_impl<color_rgba_base>(r, g, b, a);
	}

	T r, g, b, a;
	constexpr auto elems()       { return std::tie(r, g, b, a); }
	constexpr auto elems() const { return std::make_tuple(r, g, b, a); }
};

using color_rgba_u8  = vec_impl<color_rgba_base<uint8_t, 255>>;
using color_rgba_s16 = vec_impl<color_rgba_base<int16_t, 255>>;
using color_rgba_f32 = vec_impl<color_rgba_base<float, 1.0f>>;

struct uv_coord_base {
	float u, v;
	constexpr auto elems()       { return std::tie(u, v); }
	constexpr auto elems() const { return std::make_tuple(u, v); }
};

using uv_coord = vec_impl<uv_coord_base>;