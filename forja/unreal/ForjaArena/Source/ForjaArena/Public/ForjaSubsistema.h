// Autoridade da partida dentro da Unreal (servidor dedicado ou anfitrião): hospeda forja::Match, o mesmo
// núcleo do servidor de protótipo. Sincroniza posições dos personagens (CharacterMovement) com o runtime,
// aplica os deslocamentos das receitas e replica os eventos para os clientes reconstruírem efeitos.
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ForjaSubsistema.generated.h"

namespace forja { class Match; }
class AForjaPersonagem;

UCLASS()
class FORJAARENA_API UForjaSubsistema : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Colecao) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UForjaSubsistema, STATGROUP_Tickables); }

	int32 RegistrarJogador(AForjaPersonagem* Personagem, const FString& Nome, const FString& Token);
	void Desconectar(AForjaPersonagem* Personagem);
	/** Publica um pacote forja.pacote/1 (receita + dependências). O servidor recalcula os digests. */
	FString Publicar(const FString& PacoteJson);
	FString Equipar(int32 Assento, int32 Espaco, const FString& Digest);
	/** Pressionar/soltar/secundário. TCliente: relógio monotônico do próprio cliente (só diferenças importam). */
	FString Botao(int32 Assento, int32 Espaco, const FString& Tipo, int64 TCliente, float MiraYaw, const FVector& Mira);
	FString ComandoSala(int32 Assento, const FString& ComandoJson);
	int64 AgoraMs() const;

private:
	forja::Match* Partida = nullptr;
	TMap<int32, TWeakObjectPtr<AForjaPersonagem>> PorAssento;
	double T0 = 0.0;
	FString UltimaFase;
};
