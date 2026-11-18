#include "ForjaModoJogo.h"
#include "ForjaEstadoJogo.h"
#include "ForjaPersonagem.h"

AForjaModoJogo::AForjaModoJogo()
{
	DefaultPawnClass = AForjaPersonagem::StaticClass();   // troque por BP_ForjaPersonagem (MetaHuman/Mutable) no editor
	GameStateClass = AForjaEstadoJogo::StaticClass();
}
