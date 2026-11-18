// Estado replicado da sala e canal confiável dos eventos do runtime (os clientes reconstroem luz, partículas e som).
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "ForjaEstadoJogo.generated.h"

class UForjaCues;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FForjaEvento, const FString&, EventoJson);

UCLASS()
class FORJAARENA_API AForjaEstadoJogo : public AGameStateBase
{
	GENERATED_BODY()
public:
	UFUNCTION(NetMulticast, Reliable) void Multicast_Eventos(const TArray<FString>& Eventos);
	UPROPERTY(ReplicatedUsing = OnRep_Sala, BlueprintReadOnly, Category = "Forja") FString EstadoSala;
	UPROPERTY(BlueprintAssignable, Category = "Forja") FForjaEvento AoEvento;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Forja") TObjectPtr<UForjaCues> Cues;
	UFUNCTION() void OnRep_Sala();
	UFUNCTION(BlueprintImplementableEvent, Category = "Forja") void AoMudarSala(const FString& Json);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
};
