#include "Updatable.h"
PVector<Updatable> updatableList;
PVector<Updatable> safeUpdatableList;

Updatable::Updatable()
{
    updatableList.push_back(this);
}

Updatable::Updatable(bool useless)
{
	// updatableList.push_back(this);
	safeUpdatableList.push_back(this);
}

Updatable::~Updatable()
{
    //dtor
}
