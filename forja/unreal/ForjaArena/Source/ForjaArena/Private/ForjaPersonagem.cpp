#include "ForjaPersonagem.h"
#include "ForjaAtributos.h"
#include "ForjaSubsistema.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

AForjaPersonagem::AForjaPersonagem()
{
	bReplicates = true;
	Habilidades = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("Habilidades"));
	Habilidades->SetIsReplicated(true);
	Habilidades->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
	Atributos = CreateDefaultSubobject<UForjaAtributos>(TEXT("Atributos"));
}

void AForjaPersonagem::PossessedBy(AController* NovoControle)
{
	Super::PossessedBy(NovoControle);
	Habilidades->InitAbilityActorInfo(this, this);
	if (UForjaSubsistema* S = GetWorld()->GetSubsystem<UForjaSubsistema>())
	{
		const APlayerState* PS = GetPlayerState();
		const FString Nome = PS ? PS->GetPlayerName() : GetName();
		Assento = S->RegistrarJogador(this, Nome, Nome);
	}
}

void AForjaPersonagem::EndPlay(const EEndPlayReason::Type Motivo)
{
	if (HasAuthority()) if (UForjaSubsistema* S = GetWorld()->GetSubsystem<UForjaSubsistema>()) S->Desconectar(this);
	Super::EndPlay(Motivo);
}

void AForjaPersonagem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const
{
	Super::GetLifetimeReplicatedProps(Out);
	DOREPLIFETIME(AForjaPersonagem, Assento);
	DOREPLIFETIME(AForjaPersonagem, Genoma);
}

void AForjaPersonagem::Pressionar(int32 Espaco, const FString& Tipo)
{
	// O cliente responde na hora (cosmético previsto no Blueprint via AoPrever) e envia o próprio relógio:
	// o servidor julga janelas pela linha do tempo do jogador (ver docs/ARQUITETURA.md, Rede).
	const int64 TCliente = static_cast<int64>(FPlatformTime::Seconds() * 1000.0);
	const FRotator Olhar = GetControlRotation();
	const FVector Mira = GetActorLocation() + Olhar.Vector() * 800.f;
	Server_Botao(Espaco, Tipo, TCliente, Olhar.Yaw, Mira);
}

void AForjaPersonagem::Server_Botao_Implementation(int32 Espaco, const FString& Tipo, int64 TCliente, float MiraYaw, FVector_NetQuantize Mira)
{
	if (UForjaSubsistema* S = GetWorld()->GetSubsystem<UForjaSubsistema>()) Client_Resposta(S->Botao(Assento, Espaco, Tipo, TCliente, MiraYaw, Mira));
}

void AForjaPersonagem::Server_Publicar_Implementation(const FString& Pacote)
{
	if (UForjaSubsistema* S = GetWorld()->GetSubsystem<UForjaSubsistema>()) Client_Resposta(S->Publicar(Pacote));
}

void AForjaPersonagem::Server_Equipar_Implementation(int32 Espaco, const FString& Digest)
{
	if (UForjaSubsistema* S = GetWorld()->GetSubsystem<UForjaSubsistema>()) Client_Resposta(S->Equipar(Assento, Espaco, Digest));
}

void AForjaPersonagem::Server_Sala_Implementation(const FString& ComandoJson)
{
	if (UForjaSubsistema* S = GetWorld()->GetSubsystem<UForjaSubsistema>()) Client_Resposta(S->ComandoSala(Assento, ComandoJson));
}

void AForjaPersonagem::Client_Resposta_Implementation(const FString& Json) { AoResponder(Json); }
