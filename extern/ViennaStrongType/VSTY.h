#pragma once

#include <limits>
#include <utility>

namespace vsty {

	template<typename T>
	concept Hashable = requires(T a) {
		{ std::hash<T>{}(a) } -> std::convertible_to<std::size_t>;
	};


	/**
	* \brief General strong type
	*
	* T...the type
	* P...phantom type as unique ID (can use __COUNTER__ or vsty::counter<>)
	* D...default m_value (=null m_value), e.g. std::integral_constant<T, 0>
	*/
    template<typename T, auto P, typename D = void >
    struct strong_type_t {
        strong_type_t() noexcept requires (std::is_same_v<D, void>) = default;					//default constructible
        strong_type_t() noexcept requires (!std::is_same_v<D, void>) { m_value = D::value; };	//explicit from a NULL value
        explicit strong_type_t(const T& v) noexcept { m_value = v; };	//explicit from type T
        explicit strong_type_t(T&& v) noexcept { m_value = std::move(v); };	//explicit from type T

        strong_type_t( strong_type_t<T, P, D> const &) noexcept = default;		//copy constructible
        strong_type_t( strong_type_t<T, P, D>&&) noexcept = default;			//move constructible

        strong_type_t<T, P, D>& operator=(T const& v) noexcept { m_value = v; return *this; };		//copy assignable from type T
        strong_type_t<T, P, D>& operator=(T&& v) noexcept { m_value = std::move(v); return *this; };	//copy assignable from type T

        strong_type_t<T, P, D>& operator=(strong_type_t<T, P, D> const&) noexcept = default;	//move assignable
        strong_type_t<T, P, D>& operator=(strong_type_t<T, P, D>&&) noexcept = default;			//move assignable

		T& value() noexcept { return m_value; }	//get reference to the value
		operator const T& () const noexcept { return m_value; }	//retrieve m_value
		operator T& () noexcept { return m_value; }				//retrieve m_value

		auto operator<=>(const strong_type_t<T, P, D>& v) const = default;
	
		struct equal_to {
			constexpr bool operator()(const T& lhs, const T& rhs) const noexcept requires std::equality_comparable<std::decay_t<T>> { return lhs == rhs; };
		};
		
        struct hash {
            std::size_t operator()(const strong_type_t<T, P, D>& tag) const noexcept requires Hashable<std::decay_t<T>> { return std::hash<T>()(tag.m_value); };
        };

		bool has_value() const noexcept requires (!std::is_same_v<D, void>) { return m_value != D::value; }

		//-----------------------------------------------------------------------------------

		/***
 		* \brief Get bits from the m_value
		* \param first_bit...first bit to get, starts with 0
		* \param number_bits...number of bits to get
		*/
		auto get_bits(const uint32_t first_bit, const uint32_t number_bits) const noexcept -> T requires std::unsigned_integral<T>  {
			uint32_t nbits = sizeof(T) * 8;
			assert(first_bit < nbits && first_bit + number_bits <= nbits);
			if( number_bits == nbits) return m_value;
			auto val = (m_value >> first_bit) & ((1ull << number_bits) - 1);
			return val;
		}

		/***
		* \brief Set  bits in the m_value
		* \param value...value to set
		* \param first_bit...first bit to set, starts with 0
		* \param number_bits...number of bits to set	
		*/
		void set_bits(const T&& value, const uint32_t first_bit, const uint32_t number_bits) requires std::unsigned_integral<T> {
			uint32_t nbits = sizeof(T) * 8;
			assert(first_bit + number_bits <= nbits);
			assert(value < 1ull << number_bits);
			if( number_bits >= nbits) { m_value = value; return; }

			T umask = static_cast<T>(~0ull) << (first_bit + number_bits);
			T lmask = first_bit > 0ull ? (1ull << first_bit) - 1 : 0ull;

			m_value = (m_value & (umask | lmask)) | (value << first_bit);
		}

	protected:
		T m_value;
	};


	
	//--------------------------------------------------------------------------------------------
	//type counter lifted from https://mc-deltat.github.io/articles/stateful-metaprogramming-cpp20

	template<size_t N>
	struct reader { friend auto counted_flag(reader<N>); };

	template<size_t N>
	struct setter {
		friend auto counted_flag(reader<N>) {}
		static constexpr size_t n = N;
	};

	template< auto Tag, size_t NextVal = 0 >
	[[nodiscard]] consteval auto counter_impl() {
		constexpr bool counted_past_m_value = requires(reader<NextVal> r) { counted_flag(r); };

		if constexpr (counted_past_m_value) {
			return counter_impl<Tag, NextVal + 1>();
		}
		else {
			setter<NextVal> s;
			return s.n;
		}
	}

	template< auto Tag = [] {}, auto Val = counter_impl<Tag>() >
	constexpr auto counter = Val;

}


