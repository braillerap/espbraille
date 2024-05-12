import React, { Component } from "react";
import { Outlet } from "react-router-dom";

import './App.css';

class Layout extends Component 
{
  
  
  render ()
  {
    return (
      <div className="App">
        <div className="App-header">
          <Outlet />
        </div>
      </div>
    );
  }
}
export default Layout;