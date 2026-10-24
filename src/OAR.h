#pragma once

namespace bf::oar
{
    // Registra la condizione OAR BladeFireModeIsolatedOpening durante SKSE kPostLoad.
    // Restituisce false quando OAR non e' installato o l'API non e' disponibile.
    bool Register();
    bool IsAvailable();
}


