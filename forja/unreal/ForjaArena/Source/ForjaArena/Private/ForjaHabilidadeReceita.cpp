#include "ForjaHabilidadeReceita.h"
#include "ForjaPersonagem.h"

UForjaHabilidadeReceita::UForjaHabilidadeReceita()
{
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UForjaHabilidadeReceita::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info, const FGameplayAbilityActivationInfo Ativacao, const FGameplayEventData* Dados)
{
	if (!CommitAbility(Handle, Info, Ativacao)) { EndAbility(Handle, Info, Ativacao, true, true); return; }
	if (Info && Info->IsLocallyControlled())
		if (AForjaPersonagem* P = Cast<AForjaPersonagem>(Info->AvatarActor.Get())) P->Pressionar(Espaco);
	EndAbility(Handle, Info, Ativacao, true, false);
}
