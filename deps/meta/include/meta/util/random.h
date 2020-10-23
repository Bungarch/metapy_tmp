/**
 * @file random.h
 * @author Chase Geigle
 *
 * All files in META are dual-licensed under the MIT and NCSA licenses. For more
 * details, consult the file LICENSE.mit and LICENSE.ncsa in the root of the
 * project.
 */

#ifndef META_UTIL_RANDOM_H_
#define META_UTIL_RANDOM_H_

#include <cstdint>
#include <functional>
#include <limits>
#include <random>

#include "meta/config.h"

namespace meta
{

/**
 * A collection of utility classes/functions for randomness. (e.g. random
 * number generation, shuffling, etc.).
 */
namespace random
{

/**
 * A class to type-erase any unsigned random number generator in a way that
 * makes STL algorithms happy.
 */
class any_rng
{
  public:
    using result_type = std::uint64_t;

    /**
     * The minimum value generated by the RNG.
     */
    static constexpr result_type min()
    {
        return 0;
    }

    /**
     * The maximum value generated by the RNG.
     */
    static constexpr result_type max()
    {
        return std::numeric_limits<result_type>::max();
    }

    template <class RandomEngine>
    using random_engine
        = std::independent_bits_engine<RandomEngine, 64, result_type>;

    /**
     * Constructor: takes any (unsigned) random number generator and wraps
     * it in a type-erased way. any_rng always produces 64-bit random
     * numbers.
     */
    template <class RandomEngine, class = typename std::enable_if<!std::is_same<
                                      typename std::decay<RandomEngine>::type,
                                      any_rng>::value>::type>
    any_rng(RandomEngine&& rng)
        : wrapped_(random_engine<typename std::decay<RandomEngine>::type>(
              std::forward<RandomEngine>(rng)))
    {
        // nothing
    }

    /**
     * Call operator: generates one random number.
     */
    result_type operator()() const
    {
        return wrapped_();
    }

  private:
    /// the wrapped RNG
    std::function<result_type()> wrapped_;
};

/**
 * A 64-bit pseudo-random number generator that uses only a single 64-bit
 * unsigned integer as its state. Passes BigCrush. Not recommended for
 * standard use, but it is useful for taking a single 64-bit seed and
 * seeding a generator with a larger state.
 *
 * The original was written in 2015 by Sebastiano Vigna (vigna@acm.org) and
 * released into the public domain.
 *
 * @see http://dl.acm.org/citation.cfm?doid=2714064.2660195
 * @see http://xoroshiro.di.unimi.it/splitmix64.c
 */
class splitmix64
{
  public:
    using result_type = uint64_t;

    explicit splitmix64(uint64_t seed)
      : state_{seed}
    {
        // nothing
    }

    static constexpr uint64_t min()
    {
        return 0;
    }

    static constexpr uint64_t max()
    {
        return std::numeric_limits<uint64_t>::max();
    }

    inline uint64_t operator()()
    {
        uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

  private:
    uint64_t state_;
};

/**
 * A high quality 64-bit psuedo-random number generator that is the
 * successor to the xorshift128+ family. It passes BigCrush without
 * systematic failures, but has a relatively short period.
 *
 * The 128-bit state must be seeded to not be zero everywhere.
 *
 * THe original was written in 2016 by David Blackman and Sebastiano Vigna
 * (vigna@acm.org) and released into the public domain.
 *
 * @see http://xoroshiro.di.unimi.it/xoroshiro128plus.c
 */
class xoroshiro128
{
  public:
    using result_type = uint64_t;

    explicit xoroshiro128(uint64_t value)
    {
        seed(value);
    }

    explicit xoroshiro128(uint64_t s1, uint64_t s2)
      : s0_{s1}, s1_{s2}
    {
        // nothing
    }

    static constexpr uint64_t min()
    {
        return 0;
    }

    static constexpr uint64_t max()
    {
        return std::numeric_limits<uint64_t>::max();
    }

    void seed(uint64_t value)
    {
        splitmix64 sm{value};
        s0_ = sm();
        s1_ = sm();
    }

    inline uint64_t operator()()
    {
        const auto s0 = s0_;
        auto s1 = s1_;
        const auto result = s0 + s1;

        s1 ^= s0;
        s0_ = rotl(s0, 55) ^ s1 ^ (s1 << 14);
        s1_ = rotl(s1, 36);

        return result;
    }

  private:
    static inline uint64_t rotl(const uint64_t x, int k)
    {
        return (x << k) | (x >> (64 - k));
    }

    uint64_t s0_;
    uint64_t s1_;
};

/**
 * Generate a random number between 0 and an (exclusive) upper bound. This
 * uses the rejection sampling technique, and it assumes that the
 * RandomEngine has a strictly larger range than the desired one.
 *
 * @param rng The rng to generate numbers from
 * @param upper_bound The exclusive upper bound for the number
 * @return a random number in the range [0, upper_bound)
 */
template <class RandomEngine>
typename RandomEngine::result_type
bounded_rand(RandomEngine& rng, typename RandomEngine::result_type upper_bound)
{
    auto random_max = RandomEngine::max() - RandomEngine::min();
    auto threshold = random_max - (random_max + 1) % upper_bound;

    while (true)
    {
        // proposal is in the range [0, random_range]
        auto proposal = rng() - RandomEngine::min();
        if (proposal <= threshold)
            return proposal % upper_bound;
    }
}

/**
 * Shuffles the given range using the provided rng.
 *
 * THERE IS A REASON we don't use std::shuffle here: we want
 * reproducibility between compilers, who don't seem to agree on the number
 * of times to call rng_ in the shuffle process.
 *
 * Furthermore, it seems that we can't rely on a canonical number of rng_
 * calls in std::uniform_int_distribution, either, so that's out too.
 *
 * We instead use random::bounded_rand(), since we know that the range of
 * the RNG is definitely going to be larger than the upper bounds we
 * request here.
 *
 * @param first The iterator to the beginning of the range to be shuffled
 * @param last The iterator to the end of the range to be shuffled
 * @param rng The random number generator to use
 */
template <class RandomAccessIterator, class RandomEngine>
void shuffle(RandomAccessIterator first, RandomAccessIterator last,
             RandomEngine&& rng)
{
    using result_type =
        typename std::remove_reference<RandomEngine>::type::result_type;
    using difference_type =
        typename std::iterator_traits<RandomAccessIterator>::difference_type;

    auto dist = last - first;
    assert(dist > 0);
    for (difference_type i = 0; i < dist; ++i)
    {
        using std::swap;
        auto bound = static_cast<result_type>(dist - i);
        auto idx = static_cast<difference_type>(bounded_rand(rng, bound));
        swap(first[dist - 1 - i], first[idx]);
    }
}
}
}
#endif