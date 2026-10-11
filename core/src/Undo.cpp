#include "neptune/core/Undo.h"

#include <algorithm>

namespace neptune::core
{
// ---------------------------------------------------------------- SetParameterCommand

SetParameterCommand::SetParameterCommand (ParameterSet& set, ParamId id, double oldValue, double newValue, std::string name)
    : set_ (set), id_ (id), oldValue_ (oldValue), newValue_ (newValue), name_ (std::move (name)) {}

void SetParameterCommand::redo() { set_.set (id_, newValue_); }
void SetParameterCommand::undo() { set_.set (id_, oldValue_); }

bool SetParameterCommand::mergeWith (const Command& next)
{
    const auto* other = dynamic_cast<const SetParameterCommand*> (&next);
    if (other == nullptr || other->id_ != id_ || &other->set_ != &set_) return false;
    newValue_ = other->newValue_;
    return true;
}

// ---------------------------------------------------------------- CompositeCommand

void CompositeCommand::redo()
{
    for (auto& c : children_) c->redo();
}

void CompositeCommand::undo()
{
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) (*it)->undo();
}

// ---------------------------------------------------------------- UndoManager

void UndoManager::push (std::unique_ptr<Command> command)
{
    undoStack_.push_back (std::move (command));
    while (undoStack_.size() > maxDepth_) undoStack_.pop_front();
}

void UndoManager::perform (std::unique_ptr<Command> command)
{
    if (! command) return;
    command->redo();
    redoStack_.clear();
    ++revision_;

    if (transactionDepth_ > 0)
    {
        pending_->add (std::move (command));
        return;
    }

    if (gestureOpen_ && gestureHasEntry_ && ! undoStack_.empty() && undoStack_.back()->mergeWith (*command))
        return; // absorbée par l'entrée du geste en cours

    push (std::move (command));
    if (gestureOpen_) gestureHasEntry_ = true;
}

bool UndoManager::setParameter (ParameterSet& set, ParamId id, double plain)
{
    const auto* spec = set.find (id);
    if (spec == nullptr) return false;

    const double oldValue = set.get (id);
    const double newValue = spec->clamp (plain);
    if (oldValue == newValue) return false;

    perform (std::make_unique<SetParameterCommand> (set, id, oldValue, newValue, "Set " + spec->name));
    return true;
}

void UndoManager::beginGesture()
{
    gestureOpen_ = true;
    gestureHasEntry_ = false;
}

void UndoManager::endGesture()
{
    gestureOpen_ = false;
    gestureHasEntry_ = false;
}

void UndoManager::beginTransaction (std::string name)
{
    if (transactionDepth_++ == 0) pending_ = std::make_unique<CompositeCommand> (std::move (name));
}

void UndoManager::endTransaction()
{
    if (transactionDepth_ == 0) return;
    if (--transactionDepth_ > 0) return;

    auto composite = std::move (pending_);
    if (composite && composite->size() > 0)
    {
        push (std::move (composite)); // déjà exécutée commande par commande
        ++revision_;
    }
}

bool UndoManager::undo()
{
    if (! canUndo()) return false;
    auto c = std::move (undoStack_.back());
    undoStack_.pop_back();
    c->undo();
    redoStack_.push_back (std::move (c));
    gestureHasEntry_ = false;
    ++revision_;
    return true;
}

bool UndoManager::redo()
{
    if (! canRedo()) return false;
    auto c = std::move (redoStack_.back());
    redoStack_.pop_back();
    c->redo();
    undoStack_.push_back (std::move (c));
    gestureHasEntry_ = false;
    ++revision_;
    return true;
}

void UndoManager::clear()
{
    undoStack_.clear();
    redoStack_.clear();
    pending_.reset();
    transactionDepth_ = 0;
    gestureOpen_ = false;
    gestureHasEntry_ = false;
    ++revision_;
}
} // namespace neptune::core
