#ifndef UPDATABLE_H
#define UPDATABLE_H

#include "P.h"

class Updatable;
extern PVector<Updatable> updatableList;
extern PVector<Updatable> safeUpdatableList;

//Abstract class for entity that can be updated.
class Updatable: public virtual PObject
{
    public:
		Updatable();
		// For being in safeUpdatableList
		Updatable(bool useless);
        virtual ~Updatable();
        virtual void update(float delta) = 0;
		virtual void safeUpdate(float delta) { }
    protected:
    private:
};

#endif // UPDATABLE_H
