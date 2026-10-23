//
//  Random.cxx
//  RemoteInput
//
//  Created by Brandon on 2019-12-29.
//  Copyright © 2019 XIO. All rights reserved.
//

#include "Random.hxx"

std::shared_ptr<Random> Random::instance() noexcept
{
    thread_local std::shared_ptr<Random> instance{new Random()};
    return instance;
}

std::int64_t Random::generate_random_int(std::int64_t min, std::int64_t max) noexcept
{
    thread_local std::uniform_int_distribution<std::int64_t> dist{min, max};
    return dist(rng, decltype(dist)::param_type(min, max));
}

float Random::generate_random_float(float min, float max) noexcept
{
    thread_local std::uniform_real_distribution<> dist{min, std::nextafter(max, std::numeric_limits<float>::max())};
    return dist(rng, decltype(dist)::param_type(min, std::nextafter(max, std::numeric_limits<float>::max())));
}

double Random::generate_random_double(double min, double max) noexcept
{
    thread_local std::uniform_real_distribution<> dist{min, std::nextafter(max, std::numeric_limits<double>::max())};
    return dist(rng, decltype(dist)::param_type(min, std::nextafter(max, std::numeric_limits<double>::max())));
}

//mu + Normal(0, sigma) + Exponential(tau): a reaction time's shape. A sigma or tau of 0 leaves that part out (a person's
//fit can put all the spread in tau).
double Random::generate_exgaussian(double mu, double sigma, double tau) noexcept
{
    thread_local std::normal_distribution<double> normal;
    thread_local std::exponential_distribution<double> exponential;
    double gauss = sigma > 0 ? normal(rng, decltype(normal)::param_type(mu, sigma)) : mu;
    return gauss + (tau > 0 ? exponential(rng, decltype(exponential)::param_type(1.0 / tau)) : 0);
}
