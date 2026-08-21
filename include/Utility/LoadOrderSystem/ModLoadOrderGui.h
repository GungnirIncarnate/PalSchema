#pragma once

#include "Loader/PalMainLoader.h"

namespace PS {
    class ModLoadOrderGui
    {
    public:
        explicit ModLoadOrderGui(const Palworld::PalMainLoader& mainLoader);

        void RenderMods() const;

    private:
        const Palworld::PalMainLoader& m_mainLoader;
    };
}
