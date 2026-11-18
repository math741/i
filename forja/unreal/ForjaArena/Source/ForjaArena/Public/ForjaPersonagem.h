// Personagem jogável: CharacterMovement para andar; GAS para custo/recarga/tags; RPCs para a linguagem do jogador.
// Aparência: Genoma (JSON do protótipo) mapeado para MetaHuman/Mutable no Blueprint filho (BP_ForjaPersonagem).
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "ForjaPersonagem.generated.h"

class UAbilitySystemComponent;
class UForjaAtributos;

UCLASS()
class FORJAARENA_API AForjaPersonagem : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()
public:
	AForjaPersonagem();
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return Habilidades; }
	virtual void PossessedBy(AController* NovoControle) override;
	virtual void EndPlay(const EEndPlayReason::Type Motivo) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

	/** Entrada do jogador (Enhanced Input chama isto para 1–4; soltar e secundário usam Tipo). */
	UFUNCTION(BlueprintCallable, Category = "Forja") void Pressionar(int32 Espaco, const FString& Tipo = TEXT("pressionar"));

	UFUNCTION(Server, Reliable) void Server_Botao(int32 Espaco, const FString& Tipo, int64 TCliente, float MiraYaw, FVector_NetQuantize Mira);
	UFUNCTION(Server, Reliable) void Server_Publicar(const FString& Pacote);
	UFUNCTION(Server, Reliable) void Server_Equipar(int32 Espaco, const FString& Digest);
	UFUNCTION(Server, Reliable) void Server_Sala(const FString& ComandoJson);
	UFUNCTION(Client, Reliable) void Client_Resposta(const FString& Json);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Forja") TObjectPtr<UAbilitySystemComponent> Habilidades;
	UPROPERTY() TObjectPtr<UForjaAtributos> Atributos;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Forja") int32 Assento = -1;
	/** Genoma do personagem (forja.genoma, mesmo formato do protótipo web): altura, massa, pele, rosto, cabelo, roupa. */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "Forja") FString Genoma;

	UFUNCTION(BlueprintImplementableEvent, Category = "Forja") void AoResponder(const FString& Json);
};
