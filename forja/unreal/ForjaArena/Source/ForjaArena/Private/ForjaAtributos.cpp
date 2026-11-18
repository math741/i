#include "ForjaAtributos.h"
#include "Net/UnrealNetwork.h"

void UForjaAtributos::OnRep_Folego(const FGameplayAttributeData& Antigo)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UForjaAtributos, Folego, Antigo);
}

void UForjaAtributos::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const
{
	Super::GetLifetimeReplicatedProps(Out);
	DOREPLIFETIME_CONDITION_NOTIFY(UForjaAtributos, Folego, COND_None, REPNOTIFY_Always);
}
