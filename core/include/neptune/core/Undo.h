#pragma once
// Undo/redo par commandes (palier 0, point 2).
// Portée : une instance du plugin (historique propre, séparé de l'undo de l'hôte).
// Thread : interface/éditeur uniquement. Le thread audio ne passe jamais ici ; les changements venant de
// l'hôte (automation) ou du MIDI ne sont pas annulables et ne passent pas par ce gestionnaire.
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "neptune/core/Parameter.h"

namespace neptune::core
{
class Command
{
public:
    virtual ~Command() = default;

    virtual void redo() = 0; // applique (ou ré-applique) la commande
    virtual void undo() = 0; // défait exactement ce que redo() a fait
    virtual std::string name() const = 0;

    // Absorbe `next` (déjà appliquée) dans cette commande si c'est possible. Sert à fusionner les gestes
    // continus (glisser un potard = 1 seule entrée d'historique).
    virtual bool mergeWith (const Command& /*next*/) { return false; }
};

// Changement de valeur d'un paramètre (valeur réelle, déjà bornée par le cœur).
class SetParameterCommand : public Command
{
public:
    SetParameterCommand (ParameterSet& set, ParamId id, double oldValue, double newValue, std::string name);

    void redo() override;
    void undo() override;
    std::string name() const override { return name_; }
    bool mergeWith (const Command& next) override;

private:
    ParameterSet& set_;
    ParamId id_;
    double oldValue_;
    double newValue_;
    std::string name_;
};

// Remplacement d'une valeur complète (ex. la description de l'instrument) : garde l'ancienne et la nouvelle valeur
// et les réapplique par `apply`. Deux commandes consécutives de même `mergeKey` non vide (dans un même geste continu,
// voir UndoManager::beginGesture) fusionnent en une seule entrée.
template <typename T>
class SnapshotCommand : public Command
{
public:
    SnapshotCommand (T oldValue, T newValue, std::function<void (const T&)> apply, std::string name, std::string mergeKey = {})
        : old_ (std::move (oldValue)), new_ (std::move (newValue)), apply_ (std::move (apply)),
          name_ (std::move (name)), mergeKey_ (std::move (mergeKey)) {}

    void redo() override { apply_ (new_); }
    void undo() override { apply_ (old_); }
    std::string name() const override { return name_; }
    bool mergeWith (const Command& next) override
    {
        const auto* other = dynamic_cast<const SnapshotCommand<T>*> (&next);
        if (other == nullptr || mergeKey_.empty() || other->mergeKey_ != mergeKey_) return false;
        new_ = other->new_;
        return true;
    }

private:
    T old_, new_;
    std::function<void (const T&)> apply_;
    std::string name_, mergeKey_;
};

// Plusieurs commandes annulées/rétablies d'un bloc (action structurelle, « tout réinitialiser »...).
class CompositeCommand : public Command
{
public:
    explicit CompositeCommand (std::string name) : name_ (std::move (name)) {}

    void add (std::unique_ptr<Command> c) { children_.push_back (std::move (c)); }
    std::size_t size() const noexcept { return children_.size(); }

    void redo() override;
    void undo() override;
    std::string name() const override { return name_; }

private:
    std::string name_;
    std::vector<std::unique_ptr<Command>> children_;
};

class UndoManager
{
public:
    explicit UndoManager (std::size_t maxDepth = 200) : maxDepth_ (maxDepth) {}

    // Exécute la commande (redo()) puis l'enregistre. Vide la pile de rétablissement.
    void perform (std::unique_ptr<Command> command);

    // Raccourci : change un paramètre de façon annulable. Retourne false si la valeur ne change pas
    // (aucune entrée d'historique) ou si l'id est inconnu.
    bool setParameter (ParameterSet& set, ParamId id, double plain);

    // Geste continu (glisser un potard) : les changements successifs du même paramètre entre begin et end
    // sont fusionnés en une seule entrée.
    void beginGesture();
    void endGesture();

    // Transaction : tout ce qui est exécuté entre begin et end forme une seule entrée d'historique.
    // Les transactions peuvent s'imbriquer ; seule la plus externe produit une entrée.
    void beginTransaction (std::string name);
    void endTransaction();

    bool canUndo() const noexcept { return ! undoStack_.empty() && transactionDepth_ == 0; }
    bool canRedo() const noexcept { return ! redoStack_.empty() && transactionDepth_ == 0; }
    bool undo();
    bool redo();

    std::string undoName() const { return undoStack_.empty() ? std::string() : undoStack_.back()->name(); }
    std::string redoName() const { return redoStack_.empty() ? std::string() : redoStack_.back()->name(); }
    std::size_t undoSize() const noexcept { return undoStack_.size(); }
    std::size_t redoSize() const noexcept { return redoStack_.size(); }

    void clear();
    std::uint64_t revision() const noexcept { return revision_; } // change à chaque modification de l'historique

private:
    void push (std::unique_ptr<Command> command);

    std::deque<std::unique_ptr<Command>> undoStack_;
    std::vector<std::unique_ptr<Command>> redoStack_;
    std::size_t maxDepth_;

    int transactionDepth_ = 0;
    std::unique_ptr<CompositeCommand> pending_;

    bool gestureOpen_ = false;
    bool gestureHasEntry_ = false;
    std::uint64_t revision_ = 0;
};
} // namespace neptune::core
