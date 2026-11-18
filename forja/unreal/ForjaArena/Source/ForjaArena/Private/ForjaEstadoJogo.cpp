#include "ForjaEstadoJogo.h"
#include "ForjaCues.h"
#include "Net/UnrealNetwork.h"

void AForjaEstadoJogo::Multicast_Eventos_Implementation(const TArray<FString>& Eventos)
{
	for (const FString& E : Eventos)
	{
		if (Cues && GetNetMode() != NM_DedicatedServer) Cues->Executar(GetWorld(), E);
		AoEvento.Broadcast(E);
	}
}

void AForjaEstadoJogo::OnRep_Sala() { AoMudarSala(EstadoSala); }

void AForjaEstadoJogo::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const
{
	Super::GetLifetimeReplicatedProps(Out);
	DOREPLIFETIME(AForjaEstadoJogo, EstadoSala);
}
