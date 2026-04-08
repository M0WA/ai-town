#pragma once
#include "simulation_types.h"

class IEconomyQuery {
public:
    virtual ~IEconomyQuery() = default;
    virtual float getTreasuryBalance()        const = 0;
    virtual float getCurrentMonthlyRevenue()  const = 0;
    virtual float getOutstandingDebt()        const = 0;
    virtual float estimateMonthlyUpkeep()     const = 0;
    virtual void  setTaxRate(ZoneType zone, float rate) = 0;
    virtual float getTaxRate(ZoneType zone)   const = 0;
    virtual float getTaxRevenue(ZoneType zone) const = 0;
    virtual float getWagesCost()              const = 0;
    virtual float getRoadMaintenanceCost()    const = 0;
    virtual float getServiceUpkeepCost()      const = 0;
    virtual float getUtilityFeeRevenue()      const = 0;
    virtual int   getOutstandingBondUses()    const = 0;
};
