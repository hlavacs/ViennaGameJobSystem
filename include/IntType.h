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
	explicit int_type(const T& t) noexcept : value{ t } {};
	int_type(const int_type<T, P, D>& t) noexcept : value{ t.value } {};
	int_type(int_type<T, P, D>&& t) noexcept : value{ std::move(t.value) } {};
	void operator=(const int_type<T, P, D>& rhs) noexcept { value = rhs.value; };
	void operator=(int_type<T, P, D>&& rhs) noexcept { value = std::move(rhs.value); };
	auto operator<=>(const int_type<T, P, D>& v) const = default;
	auto operator<=>(const T& v) noexcept { return value <=> v; };

	struct hash {
		std::size_t operator()(const int_type<T, P, D>& tg) const { return std::hash<T>()(tg.value); };
	};

	struct equal_to {
		constexpr bool operator()(const T& lhs, const T& rhs) const { return lhs == rhs; };
	};

	bool is_null() const {
		return value == null;
	}
};


#endif
