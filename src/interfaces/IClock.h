#pragma once

class IClock {
public:
    virtual ~IClock() = default;
    virtual double nowSeconds() const = 0;  // wall-clock seconds since epoch or arbitrary start
};
