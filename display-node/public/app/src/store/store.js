/**
 * Created by turui on 2/7/2017.
 */
import { applyMiddleware, createStore } from 'redux';
import { iotControllerApp } from '../reducers/reducers';
import { refreshDeviceList } from '../actions/actions';
import logger from 'redux-logger';
import thunk from 'redux-thunk';

const middleware = applyMiddleware(thunk, logger());
const store = createStore(iotControllerApp, middleware);

/*
let unsubscribe = store.subscribe(() =>
    console.log(store.getState())
);
*/

setInterval( () => {
    store.dispatch((dispatch)=> {
        (refreshDeviceList())(dispatch);
    });
}, 1000);

export default store;