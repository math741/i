// Atributos GAS: fôlego (custo das criações). O valor autoritativo vem do runtime do núcleo a cada tick.
#pragma once
#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "ForjaAtributos.generated.h"

#define FORJA_ATRIBUTO(Classe, Nome) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(Classe, Nome) GAMEPLAYATTRIBUTE_VALUE_GETTER(Nome) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(Nome) GAMEPLAYATTRIBUTE_VALUE_INITTER(Nome)

UCLASS()
class FORJAARENA_API UForjaAtributos : public UAttributeSet
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Folego, Category = "Forja")
	FGameplayAttributeData Folego = 100.f;
	FORJA_ATRIBUTO(UForjaAtributos, Folego)

	UFUNCTION() void OnRep_Folego(const FGameplayAttributeData& Antigo);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
};
