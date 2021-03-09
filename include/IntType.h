#ifndef INTTYPE_H
#define INTTYPE_H

template<typename T, typename P, auto D = -1>
struct int_type {
	using type_name = T;
	static const T null = static_cast<T>(D);

	T value{};
	int_type() {
		static_assert(!(std::is_unsigned_v<T> && std::is_signed_v<decltype(D)> && static_cast<int>(D) < 0));
		value = static_cast<T>(D);
	};

	template<typename U>
	requires std::is_convertible_v<U, T>
	explicit int_type(const U& t) noexcept : value{ static_cast<T>(t) } {};

	int_type(const int_type<T, P, D>& t) noexcept : value{ t.value } {};
	int_type(int_type<T, P, D>&& t) noexcept : value{ std::move(t.value) } {};

	void operator=(const int_type<T, P, D>& rhs) noexcept { value = rhs.value; };
	void operator=(int_type<T, P, D>&& rhs) noexcept { value = std::move(rhs.value); };

	template<typename U>
	requires std::is_convertible_v<U, T>
	void operator=(const U& rhs) noexcept { value = static_cast<T>(rhs); };

	operator const T& () const { return value; } 
	operator T& () { return value; }

	auto operator<=>(const int_type<T, P, D>& v) const = default;

	template<typename U>
	requires std::is_convertible_v<U, T>
	auto operator<(const U& v) noexcept { return value <=> static_cast<T>(v); };

	T operator<<(const size_t L) noexcept { return value << L; };
	T operator>>(const size_t L) noexcept { return value >> L; };
	T operator&(const size_t L) noexcept { return value & L; };
	int_type<T, P, D> operator++() noexcept {
		value++; 
		if( !has_value() ) value = 0;
		return *this;
	};

	int_type<T, P, D> operator++(int i) noexcept {
		int_type<T, P, D> res = *this;
		value++;
		if (!has_value()) value = 0;
		return res;
	};

	int_type<T, P, D>& operator--() noexcept { --value; return *this;  };

	struct hash {
		std::size_t operator()(const int_type<T, P, D>& tg) const { return std::hash<T>()(tg.value); };
	};

	struct equal_to {
		constexpr bool operator()(const T& lhs, const T& rhs) const { return lhs == rhs; };
	};

	bool has_value() const {
		return value != null;
	}
};


#endif
