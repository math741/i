// UMA habilidade GAS para todas as criações: valida fôlego e recarga (tags) e entrega a ativação ao núcleo.
// O GAS não conhece Passo, Pulso nem nenhuma criação; o comportamento é o plano compilado da receita.
#pragma once
#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "ForjaHabilidadeReceita.generated.h"

UCLASS()
class FORJAARENA_API UForjaHabilidadeReceita : public UGameplayAbility
{
	GENERATED_BODY()
public:
	UForjaHabilidadeReceita();
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Forja") int32 Espaco = 0;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info, const FGameplayAbilityActivationInfo Ativacao, const FGameplayEventData* Dados) override;
};
