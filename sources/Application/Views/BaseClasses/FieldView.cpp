#include "FieldView.h"
#include "System/Console/Trace.h"

FieldView::FieldView(GUIWindow &w,ViewData *data):View(w,data),T_SimpleList<UIField>(true) {
	focus_=0 ;	
	prefX_=-1 ;
} ;

void FieldView::SetFocus(UIField *field) {

	if (focus_) {
		focus_->ClearFocus() ;
	}
	focus_=field ;
	prefX_=-1 ;

//  Empty field view, we don't have anything to do

	if (focus_==0) return ;

	focus_->SetFocus() ;

} ;

void FieldView::ClearFocus() {
	if (focus_) {
		focus_->ClearFocus() ;
	} ;
	focus_=0 ;
} ;

UIField *FieldView::GetFocus() {
    return focus_ ;
} ;

void FieldView::Redraw() {

	if (focus_==0) {
		SetFocus(T_SimpleList<UIField>::GetFirst()) ;
	}

	IteratorPtr<UIField> it(T_SimpleList<UIField>::GetIterator()) ;

	for (it->Begin();!it->IsDone();it->Next()) {
		UIField &current=it->CurrentItem() ;
		current.Draw(w_) ;
	} ;
};

// trk2als: ¿a está más cerca que b de la columna del campo con el foco?
bool FieldView::closerX(UIField &a,UIField &b) {
	// la columna "deseada" se recuerda al subir y bajar, así no se pasa de lado
	int fx=(prefX_>=0)?prefX_:focus_->GetPosition()._x ;
	int da=a.GetPosition()._x-fx ; if (da<0) da=-da ;
	int db=b.GetPosition()._x-fx ; if (db<0) db=-db ;
	return da<db ;
}

void FieldView::ProcessButtonMask(unsigned short mask) {

	if (focus_==0) {
		focus_=T_SimpleList<UIField>::GetFirst() ;
	//  Empty field view, we don't have anything to do
		if (focus_==0) return ;
 		focus_->SetFocus() ;
	}

	
	if (mask&EPBM_A) {  // A or A+ARROW is sent to the field
		if (mask&EPBM_DOWN) {
			focus_->ProcessArrow(EPBM_DOWN) ;
			isDirty_=true ;
		}
		if (mask&EPBM_UP){
			focus_->ProcessArrow(EPBM_UP)  ;
			isDirty_=true ;
		}

		if (mask&EPBM_LEFT) {
			focus_->ProcessArrow(EPBM_LEFT) ;
			isDirty_=true ;
		}

		if (mask&EPBM_RIGHT){
			focus_->ProcessArrow(EPBM_RIGHT)  ;
			isDirty_=true ;
		}

		if (mask==EPBM_A) {
			focus_->OnClick() ;
		};

	} else {
		if (mask&EPBM_B) {  // B or B+ARROW is sent to the field

			if (mask==EPBM_B) {
				focus_->OnBClick() ;
				isDirty_=true ;
			};

			if (mask&EPBM_DOWN) {
				focus_->ProcessBArrow(EPBM_DOWN) ;
				isDirty_=true ;
			}
			if (mask&EPBM_UP){
				focus_->ProcessBArrow(EPBM_UP)  ;
				isDirty_=true ;
			}

			if (mask&EPBM_LEFT) {
				focus_->ProcessBArrow(EPBM_LEFT) ;
				isDirty_=true ;
			}

			if (mask&EPBM_RIGHT){
				focus_->ProcessBArrow(EPBM_RIGHT)  ;
				isDirty_=true ;
			}

		} else { // Nor B or A is pressed

			if (!(mask&(EPBM_A|EPBM_B|EPBM_L|EPBM_R|EPBM_SELECT|EPBM_START))) {

				if (mask&EPBM_DOWN) {
					UIField *next=0 ;
					UIField *first=0 ;

					GUIPoint focusPos=focus_->GetPosition() ;

					IteratorPtr<UIField> it(T_SimpleList<UIField>::GetIterator()) ;
					for (it->Begin();!it->IsDone();it->Next()) {
						UIField &current=it->CurrentItem() ;
						if (!current.IsStatic()) {
							if (first) {
								if (current.GetPosition()._y<first->GetPosition()._y) {
									first=&current ;
								} ;
							} else {
								first=&current ;
							}
							if (current.GetPosition()._y>focus_->GetPosition()._y) {
								if (next) {
									if (current.GetPosition()._y<next->GetPosition()._y) {
										next=&current ;
									} else if (current.GetPosition()._y==next->GetPosition()._y &&
									           closerX(current,*next)) {
										next=&current ; // trk2als: misma fila, la columna más cercana
									} ;
								} else {
									next=&current ;
								};
							} ;

						}
					}
					if (next==0) {
						next=first ;
					}

					focus_->ClearFocus() ;
					if (prefX_<0) prefX_=focus_->GetPosition()._x ;
					focus_=next ;
					focus_->SetFocus() ;
					isDirty_=true ;
				}


				if (mask&EPBM_UP){

					UIField *prev=0 ;
					UIField *last=0 ;

					IteratorPtr<UIField> it(T_SimpleList<UIField>::GetIterator()) ;
					for (it->Begin();!it->IsDone();it->Next()) {
						UIField &current=it->CurrentItem() ;
						if (!current.IsStatic()) {
							if (last) {
								if (current.GetPosition()._y>last->GetPosition()._y) {
									last=&current ;
								} ;
							} else {
								last=&current ;
							}
							if (current.GetPosition()._y<focus_->GetPosition()._y) {
								if (prev) {
									if (current.GetPosition()._y>prev->GetPosition()._y) {
										prev=&current ;
									} else if (current.GetPosition()._y==prev->GetPosition()._y &&
									           closerX(current,*prev)) {
										prev=&current ; // trk2als: misma fila, la columna más cercana
									} ;
								} else {
									prev=&current ;
								};
							} ;

						}
					}
					if (prev==0) {
						prev=last ;
					}

					focus_->ClearFocus() ;
					if (prefX_<0) prefX_=focus_->GetPosition()._x ;
 					focus_=prev ;
					focus_->SetFocus() ;
					isDirty_=true ;
				}

				if (mask&EPBM_RIGHT) {
					UIField *next=0 ;
					UIField *first=0 ;

					GUIPoint focusPos=focus_->GetPosition() ;

					IteratorPtr<UIField> it(T_SimpleList<UIField>::GetIterator()) ;
					for (it->Begin();!it->IsDone();it->Next()) {
						UIField &current=it->CurrentItem() ;
						if (!current.IsStatic()&&(current.GetPosition()._y==focus_->GetPosition()._y)) {
							if (first) {
								if (current.GetPosition()._x<first->GetPosition()._x) {
									first=&current ;
								} ;
							} else {
								first=&current ;
							}
							if (current.GetPosition()._x>focus_->GetPosition()._x) {
								if (next) {
									if (current.GetPosition()._x<next->GetPosition()._x) {
										next=&current ;
									} else {
										// if both target at same height
									} ;
								} else {
									next=&current ;
								};
							} ;

						}
					}
					if (next==0) {
						next=first ;
					}

					focus_->ClearFocus() ;
					focus_=next ;
					prefX_=focus_->GetPosition()._x ;
					focus_->SetFocus() ;
					isDirty_=true ;
				}

				if (mask&EPBM_LEFT){

					UIField *prev=0 ;
					UIField *last=0 ;

					IteratorPtr<UIField> it(T_SimpleList<UIField>::GetIterator()) ;
					for (it->Begin();!it->IsDone();it->Next()) {
						UIField &current=it->CurrentItem() ;
						if (!current.IsStatic()&&(current.GetPosition()._y==focus_->GetPosition()._y)) {
							if (last) {
								if (current.GetPosition()._x>last->GetPosition()._x) {
									last=&current ;
								} ;
							} else {
								last=&current ;
							}
							if (current.GetPosition()._x<focus_->GetPosition()._x) {
								if (prev) {
									if (current.GetPosition()._x>prev->GetPosition()._x) {
										prev=&current ;
									} else {
										// if both target at same height
									} ;
								} else {
									prev=&current ;
								};
							} ;

						}
					}
					if (prev==0) {
						prev=last ;
					}

					focus_->ClearFocus() ;
 					focus_=prev ;
					prefX_=focus_->GetPosition()._x ;
					focus_->SetFocus() ;
					isDirty_=true ;
				}
			}
		}
	}
}

int FieldView::GetFocusIndex() {

	int focusIndex=0 ;
	IteratorPtr<UIField> it(T_SimpleList<UIField>::GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		if (&(it->CurrentItem())==focus_) {
			break ;
		} ;
		focusIndex++ ;
	} ;
	return focusIndex ;
}
