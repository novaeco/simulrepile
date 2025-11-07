#pragma once

/*
 * Ce fichier agit comme un simple relais vers la définition partagée du
 * protocole Core Link située dans firmware/common/include/. Cela garantit
 * que l'afficheur et le DevKitC utilisent exactement la même description
 * des messages (deltas, commandes, etc.).
 */

#include "../../common/include/link/core_link_protocol.h"
