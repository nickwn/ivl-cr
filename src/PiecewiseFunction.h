#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>

#include <gl/glew.h>
#include <glm/glm.hpp>

#include "GLObjects.h"

template<typename K, typename V>
struct LinearInterp
{
	inline V operator()(const K& k, const K& k0, const V& v0, const K& k1, const V& v1) const
	{
		const float p = (k - k0) / (k1 - k0);
		return v0 * (1.f - p) + v1 * p;
	}
};

template<typename K, typename V>
struct ConstInterp
{
	inline V operator()(const K& k, const K& k0, const V& v0, const K& k1, const V& v1) const 
	{
		return v0;
	}
};

template<typename K, typename V, typename Interp>
class PiecewiseFunction
{
public:
	PiecewiseFunction()
		: mInterp()
		, mKeys()
		, mValues()
	{}

	void AddStop(const K& k, const V& v)
	{
		const auto lowerIt = std::lower_bound(std::begin(mKeys), std::end(mKeys), k);
		const size_t idx = std::distance(std::begin(mKeys), lowerIt);
		mKeys.insert(lowerIt, k);
		mValues.insert(std::next(std::begin(mValues), idx), v);
	}

	void EvaluateTexture(const uint32_t size)
	{
		std::vector<V> evals = Evaluate(size);
		GenTexture(evals);
	}

	GLuint GetTexture() const { return mTexture; }

private:

	std::vector<V> Evaluate(const uint32_t size) const
	{
		if (mKeys.size() < 2 || mValues.size() < 2) return std::vector<V>(size);

		std::vector<V> evals(size);
		uint32_t kIdx = 0;
		const K kStep = mKeys.back() / float(size);
		K kItr = mKeys.front();
		for (uint32_t i = 0; i < size; i++)
		{
			evals[i] = mInterp(kItr, mKeys[kIdx], mValues[kIdx], mKeys[kIdx + 1], mValues[kIdx + 1]);
			kItr += kStep;
			if (mKeys.size() > kIdx + 1 && mKeys[kIdx + 1] < kItr) kIdx++;
		}

		return evals;
	}

	void GenTexture(const std::vector<V>& evals) const;

	Interp mInterp;
	
	UniqueTexture mUniqueTexture;

	std::vector<K> mKeys;
	std::vector<V> mValues;

};

template<typename K, typename V>
using PLF = PiecewiseFunction<K, V, LinearInterp<K, V>>;

template<typename K, typename V>
using PCF = PiecewiseFunction<K, V, ConstInterp<K, V>>;
